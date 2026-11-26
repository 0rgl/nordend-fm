#include "nordend/fs/origin.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace nordend::fs {
namespace {

// A URL longer than this is not kept as it is: a copy gets kExtractedOrigin instead, which asks
// all the same.
constexpr std::size_t kMaxOrigin = 4096;

// forget_origin on an open file.
std::error_code forget_origin_of(int fd) {
    // Looked at first: removing needs write permission even when there is nothing to remove,
    // and a read-only file of the user's own may still be allowed to run.
    if (::fgetxattr(fd, kOriginAttribute, nullptr, 0) < 0) {
        return {};
    }
    if (::fremovexattr(fd, kOriginAttribute) == 0 || errno != EACCES) {
        return {};  // removed, or it cannot be (a read-only disk): the file is as it was
    }
    // A read-only program of the user's own (tarballs pack them 0555): the owner may give it
    // write permission for the moment, as chmod would let them anyway. Not with setuid or
    // setgid: a chmod by an owner outside the file's group would drop setgid on the way.
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() ||
        (info.st_mode & (S_IWUSR | S_ISUID | S_ISGID)) != 0 ||
        ::fchmod(fd, info.st_mode | S_IWUSR) != 0) {
        return {};
    }
    static_cast<void>(::fremovexattr(fd, kOriginAttribute));
    if (::fchmod(fd, info.st_mode & 07777U) != 0) {
        return {errno, std::generic_category()};  // left writable
    }
    return {};
}

}  // namespace

std::optional<std::string> origin_of(const std::filesystem::path& path) {
    std::array<char, kMaxOrigin> value{};
    const ssize_t size = ::getxattr(path.c_str(), kOriginAttribute, value.data(), value.size());
    if (size <= 0) {
        return std::nullopt;
    }
    return std::string{value.data(), static_cast<std::size_t>(size)};
}

bool has_origin(int fd) noexcept { return ::fgetxattr(fd, kOriginAttribute, nullptr, 0) >= 0; }

bool set_origin(int fd, std::string_view value) noexcept {
    return ::fsetxattr(fd, kOriginAttribute, value.data(), value.size(), 0) == 0;
}

bool copy_origin(int from, int to) noexcept {
    std::array<char, kMaxOrigin> value{};
    const ssize_t size = ::fgetxattr(from, kOriginAttribute, value.data(), value.size());
    if (size < 0) {
        if (errno != ERANGE) {
            return true;  // none to give (or none that can be read)
        }
        return set_origin(to, kExtractedOrigin);
    }
    return set_origin(to, std::string_view{value.data(), static_cast<std::size_t>(size)});
}

std::error_code forget_origin(const std::filesystem::path& path) {
    // Opened once, and everything done on that descriptor: a path swapped for a link between two
    // steps cannot lead the chmod to another file. A file that cannot be read keeps its mark:
    // reading a user attribute needs read permission as well.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return {};
    }
    const std::error_code error = forget_origin_of(fd);
    ::close(fd);
    return error;
}

}  // namespace nordend::fs
