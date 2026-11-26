// A preview helper that misbehaves on purpose, for tests/helpers_test.cpp. Its last argument
// says how: crash (aborts on a request), hang (never answers), garbage (answers nonsense),
// big-lie (claims a picture larger than the memfd it sends), goodbye (says goodbye instead of
// answering, as a helper leaving after its idle time would), no-sandbox (exits as a helper that
// could not build its cage).
// Sizes that do not hold: too-wide (a picture wider than asked for), wrap (2^31 x (2^31 + 2)
// pixels, a size that wraps in 64 bits to a sparse 16 GiB memfd), unsealed (pixels that can
// still be written to).
// text-huge: the most bytes and spans a text can say, in 4 bytes. text-sparse: every line
// max_line_bytes long at the default limits, about 3.5 GB, in a sparse memfd of that size.
// entry-long: an archive entry with more written than said; entry-later: an entry written as
// said, then more through a copy of its descriptor at the next request.
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>

#include "protocol.hpp"

namespace protocol = nordend::isolate::protocol;

namespace {

// A picture of width x height in a memfd of size bytes with seals: false when it cannot be made.
bool send_pixels(std::uint32_t width, std::uint32_t height, off_t size, int seals) {
    const int memfd = ::memfd_create("lie", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (memfd < 0 || ::ftruncate(memfd, size) != 0) {
        return false;
    }
    ::fcntl(memfd, F_ADD_SEALS, seals);
    protocol::Response response;
    response.width = width;
    response.height = height;
    const std::array<int, 1> fds{memfd};
    static_cast<void>(
        protocol::send_packet(protocol::kSocketFd, std::as_bytes(std::span{&response, 1}), fds));
    ::close(memfd);
    return true;
}

// bytes written to fd, as far as they go: these lies need no more care.
void write_out(int fd, std::string_view bytes) {
    const ssize_t wrote = ::write(fd, bytes.data(), bytes.size());
    static_cast<void>(wrote);
}

// An archive read answered with the bytes asked for and their count, then more: at once
// (entry-long), or at the next request through a copy of the descriptor kept (entry-later).
void answer_entry(const protocol::Packet& packet, std::string_view mode, int& kept) {
    const std::string_view more = "more";
    if (kept >= 0) {
        write_out(kept, more);
        ::close(kept);
        kept = -1;
    }
    protocol::ArchiveReadRequest request;
    if (packet.length < sizeof(request) || packet.fds[1] < 0) {
        return;
    }
    std::memcpy(&request, packet.bytes.data(), sizeof(request));
    const std::string bytes(request.size, 'x');
    write_out(packet.fds[1], bytes);
    if (mode == "entry-long") {
        write_out(packet.fds[1], more);
    } else {
        kept = ::dup(packet.fds[1]);
    }
    protocol::ArchiveReadResponse response;
    response.bytes = request.size;
    static_cast<void>(
        protocol::send_packet(protocol::kSocketFd, std::as_bytes(std::span{&response, 1})));
}

}  // namespace

int main(int argc, char** argv) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::string_view mode{argv[argc - 1]};
    if (mode == "no-sandbox") {
        return protocol::kSandboxFailedExit;  // as a helper that could not build its cage
    }
    std::signal(SIGPIPE, SIG_IGN);  // NOLINT(cert-err33-c)
    while (true) {
        std::optional<protocol::Packet> packet = protocol::receive_packet(protocol::kSocketFd);
        if (!packet) {
            return 0;
        }
        if (mode == "entry-long" || mode == "entry-later") {
            static int kept = -1;
            answer_entry(*packet, mode, kept);
            packet->close_fds();
            continue;
        }
        packet->close_fds();
        if (mode == "crash") {
            std::abort();
        }
        if (mode == "hang") {
            while (true) {
                ::pause();
            }
        }
        if (mode == "garbage") {
            const std::array<std::byte, 7> junk{};
            static_cast<void>(protocol::send_packet(protocol::kSocketFd, junk));
            continue;
        }
        if (mode == "goodbye") {
            const protocol::Request goodbye{.magic = protocol::kGoodbyeMagic};
            static_cast<void>(
                protocol::send_packet(protocol::kSocketFd, std::as_bytes(std::span{&goodbye, 1})));
            return 0;
        }
        if (mode == "text-lie") {
            // Three lines said, two sent.
            const std::string_view two = "a\nb";
            const int memfd = ::memfd_create("lie", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            if (memfd < 0 ||
                ::write(memfd, two.data(), two.size()) != static_cast<ssize_t>(two.size())) {
                return 1;
            }
            ::fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
            protocol::TextResponse response;
            response.lines = 3;
            response.bytes = static_cast<std::uint32_t>(two.size());
            const std::array<int, 1> fds{memfd};
            static_cast<void>(protocol::send_packet(protocol::kSocketFd,
                                                    std::as_bytes(std::span{&response, 1}), fds));
            ::close(memfd);
            continue;
        }
        constexpr int all_seals = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
        if (mode == "too-wide") {
            if (!send_pixels(200, 1, 800, all_seals)) {
                return 1;
            }
            continue;
        }
        if (mode == "wrap") {
            if (!send_pixels(1U << 31U, (1U << 31U) + 2, off_t{1} << 34, all_seals)) {
                return 1;
            }
            continue;
        }
        if (mode == "unsealed") {
            if (!send_pixels(1, 1, 4, F_SEAL_SHRINK | F_SEAL_GROW)) {
                return 1;
            }
            continue;
        }
        if (mode == "text-sparse") {
            protocol::TextResponse response;
            response.lines = 256 * 1024;                   // the default max_bytes, a line each
            response.bytes = response.lines * (1024 + 1);  // the default max_line_bytes and '\n'
            response.spans = response.bytes;
            const int memfd = ::memfd_create("lie", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            if (memfd < 0 || ::ftruncate(memfd, static_cast<off_t>(protocol::text_payload_size(
                                                    response.bytes, response.spans))) != 0) {
                return 1;
            }
            ::fcntl(memfd, F_ADD_SEALS, all_seals);
            const std::array<int, 1> fds{memfd};
            static_cast<void>(protocol::send_packet(protocol::kSocketFd,
                                                    std::as_bytes(std::span{&response, 1}), fds));
            ::close(memfd);
            continue;
        }
        if (mode == "text-huge") {
            const int memfd = ::memfd_create("lie", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            if (memfd < 0 || ::ftruncate(memfd, 4) != 0) {
                return 1;
            }
            ::fcntl(memfd, F_ADD_SEALS, all_seals);
            protocol::TextResponse response;
            response.lines = 1;
            response.bytes = std::numeric_limits<std::uint32_t>::max();
            response.spans = std::numeric_limits<std::uint32_t>::max();
            const std::array<int, 1> fds{memfd};
            static_cast<void>(protocol::send_packet(protocol::kSocketFd,
                                                    std::as_bytes(std::span{&response, 1}), fds));
            ::close(memfd);
            continue;
        }
        // big-lie: 4 bytes of pixels for a 1000x1000 picture.
        const int memfd = ::memfd_create("lie", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (memfd < 0 || ::ftruncate(memfd, 4) != 0) {
            return 1;
        }
        ::fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
        protocol::Response response;
        response.width = 1000;
        response.height = 1000;
        const std::array<int, 1> fds{memfd};
        static_cast<void>(protocol::send_packet(protocol::kSocketFd,
                                                std::as_bytes(std::span{&response, 1}), fds));
        ::close(memfd);
    }
}
