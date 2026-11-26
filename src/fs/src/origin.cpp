#include "nordend/fs/origin.hpp"

#include <array>
#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace nordend::fs {
namespace {

// A URL longer than this is not kept as it is: a copy gets kExtractedOrigin instead, which asks
// all the same.
constexpr std::size_t kMaxOrigin = 4096;

// No such attribute, or no user attributes on that file system (ENOTSUP is EOPNOTSUPP on Linux).
bool absent(int error) noexcept { return error == ENODATA || error == ENOTSUP; }

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
    // Looked at first: removing needs write permission even when there is nothing to remove,
    // and a read-only file of the user's own may still be allowed to run.
    if (::getxattr(path.c_str(), kOriginAttribute, nullptr, 0) < 0) {
        return {};
    }
    if (::removexattr(path.c_str(), kOriginAttribute) == 0 || absent(errno)) {
        return {};
    }
    const int error = errno;
    // A read-only program of the user's own (tarballs pack them 0555): the owner may give it
    // write permission for the moment, as chmod would let them anyway. Not with setuid or
    // setgid: a chmod by an owner outside the file's group would drop setgid on the way.
    struct stat info{};
    if (error != EACCES || ::stat(path.c_str(), &info) != 0 || info.st_uid != ::geteuid() ||
        (info.st_mode & (S_IWUSR | S_ISUID | S_ISGID)) != 0 ||
        ::chmod(path.c_str(), info.st_mode | S_IWUSR) != 0) {
        return {error, std::generic_category()};
    }
    const bool removed = ::removexattr(path.c_str(), kOriginAttribute) == 0 || absent(errno);
    const int after = errno;
    ::chmod(path.c_str(), info.st_mode & 07777U);
    return removed ? std::error_code{} : std::error_code{after, std::generic_category()};
}

}  // namespace nordend::fs
