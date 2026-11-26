// Tries one thing inside the preview helper's cage and says what happened, for
// tests/sandbox_test.cpp. Its argument says what: allowed (what a picture needs), open, stat,
// socket, exec, fork, memory (5 GiB), mmap-exec and mprotect-exec (executable memory),
// implied-exec (READ_IMPLIES_EXEC set before the cage: a writable page run as code), or landlock
// (landlock alone: a file and a TCP connection).
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <netinet/in.h>
#include <string_view>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

#include "sandbox.hpp"

namespace isolate = nordend::isolate;

namespace {

// Printed with write: stdio's buffers would need nothing more, but stay simple.
// A failed write exits with 2: the test then sees no answer and fails.
void say(std::string_view text) {
    if (::write(STDOUT_FILENO, text.data(), text.size()) != static_cast<ssize_t>(text.size()) ||
        ::write(STDOUT_FILENO, "\n", 1) != 1) {
        std::_Exit(2);
    }
}

void say_errno(int result) {
    if (result >= 0) {
        say("ok");
    } else if (errno == EACCES) {
        say("EACCES");
    } else {
        say("other error");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2;
    }
    const std::string_view what{
        argv[1]};  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (what == "implied-exec") {
        ::personality(READ_IMPLIES_EXEC);
        if (!isolate::enter_sandbox(isolate::Unexpected::kill).seccomp) {
            say("no sandbox");
            return 1;
        }
        auto* page = static_cast<unsigned char*>(
            ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (page == MAP_FAILED) {
            say("no page");
            return 1;
        }
#if defined(__x86_64__)
        page[0] = 0xc3;  // ret
#elif defined(__aarch64__)
        const std::uint32_t ret = 0xd65f03c0;
        std::memcpy(page, &ret, sizeof(ret));
        __builtin___clear_cache(reinterpret_cast<char*>(page),  // NOLINT: code as bytes
                                reinterpret_cast<char*>(page) + 4);
#endif
        reinterpret_cast<void (*)()>(page)();  // NOLINT: SIGSEGV unless the page runs
        say("executed");
        return 0;
    }
    if (what == "landlock") {
        // A file sure to exist on any system (no /etc/hostname in Nix's build sandbox): the
        // probe itself, so landlock refuses it rather than the lookup failing first.
        std::error_code error;
        const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
        if (error) {
            say("no /proc/self/exe");
            return 1;
        }
        if (isolate::restrict_file_system_and_network() == 0) {
            say("no landlock");
            return 0;
        }
        say_errno(::open(self.c_str(), O_RDONLY | O_CLOEXEC));
        const int tcp = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(9);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        say_errno(::connect(tcp, reinterpret_cast<sockaddr*>(&address),  // NOLINT: sockets
                            sizeof(address)));
        return 0;
    }
    const isolate::SandboxStatus status = isolate::enter_sandbox(isolate::Unexpected::kill);
    if (!status.seccomp) {
        say("no sandbox");
        return 1;
    }
    if (what == "allowed") {
        const int fd = ::memfd_create("probe", MFD_CLOEXEC);
        // Memory that is not executable can still be mapped and protected.
        void* page =
            ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool mapped = page != MAP_FAILED && ::mprotect(page, 4096, PROT_READ) == 0;
        void* memory = std::malloc(std::size_t{64} << 20);  // NOLINT: the allocator's calls
        std::memset(memory, 1, std::size_t{64} << 20);
        std::free(memory);  // NOLINT
        struct stat info{};
        const bool fine = mapped && fd >= 0 && ::ftruncate(fd, 4096) == 0 &&
                          ::fstat(fd, &info) == 0 && info.st_size == 4096;
        say(fine ? "ok" : "failed");
    } else if (what == "open") {
        say_errno(::open("/etc/hostname", O_RDONLY | O_CLOEXEC));
    } else if (what == "stat") {
        struct stat info{};
        say_errno(::stat("/etc", &info));
    } else if (what == "socket") {
        say_errno(::socket(AF_INET, SOCK_STREAM, 0));  // killed before it returns
    } else if (what == "exec") {
        ::execl("/bin/true", "true", nullptr);  // killed
        say("returned");
    } else if (what == "fork") {
        say_errno(::fork());  // killed
    } else if (what == "mmap-exec") {
        void* code = ::mmap(nullptr, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                            0);  // killed
        say(code == MAP_FAILED ? "refused" : "granted");
    } else if (what == "mprotect-exec") {
        void* page =
            ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) {
            say("no page");
            return 1;
        }
        say_errno(::mprotect(page, 4096, PROT_READ));              // ok
        say_errno(::mprotect(page, 4096, PROT_READ | PROT_EXEC));  // killed
    } else if (what == "memory") {
        void* huge = ::mmap(nullptr, std::size_t{5} << 30, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        say(huge == MAP_FAILED ? "refused" : "granted");
    }
    return 0;
}
