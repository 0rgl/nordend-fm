#include "nordend/fs/permissions.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "nordend/fs/origin.hpp"

namespace nordend::fs {
namespace {

constexpr std::uint16_t kAnyExecute = S_IXUSR | S_IXGRP | S_IXOTH;

std::string error_text(const std::filesystem::path& path, int code) {
    return std::format("{}: {}", path.filename().string(),
                       std::error_code{code, std::generic_category()}.message());
}

}  // namespace

std::uint16_t running_mode(std::uint16_t mode, bool allow) noexcept {
    return static_cast<std::uint16_t>(allow ? (mode | S_IXUSR) : (mode & ~kAnyExecute));
}

ModeResult set_running(const std::vector<std::filesystem::path>& files, bool allow) {
    ModeResult result;
    for (const std::filesystem::path& file : files) {
        struct stat info{};
        if (::stat(file.c_str(), &info) != 0) {
            result.errors.push_back(error_text(file, errno));
            continue;
        }
        if (!S_ISREG(info.st_mode)) {
            continue;  // a folder's execute bit means something else
        }
        const auto before = static_cast<std::uint16_t>(info.st_mode & 07777U);
        const std::uint16_t after = running_mode(before, allow);
        // Someone else's program the user may already run (a group's execute bit) gets no
        // chmod, which would fail: only its mark goes, where it can.
        const bool runs = allow && info.st_uid != ::geteuid() && ::access(file.c_str(), X_OK) == 0;
        if (after != before && !runs) {
            if (::chmod(file.c_str(), after) != 0) {
                result.errors.push_back(error_text(file, errno));
                continue;
            }
            result.changed.push_back({.path = file, .before = before, .after = after});
        }
        // Allowed by the user: it no longer asks before it runs (issue #65). Where the mark
        // cannot go (someone else's file, a read-only disk), that is said; Enter keeps asking,
        // and the program still runs once the user says yes.
        if (allow) {
            if (const std::error_code error = forget_origin(file)) {
                result.errors.push_back(error_text(file, error.value()));
            } else if (origin_of(file)) {
                result.errors.push_back(std::format("{}: it will keep asking before it runs",
                                                    file.filename().string()));
            }
        }
    }
    return result;
}

ModeResult restore_modes(const std::vector<ModeChange>& changes, bool before) {
    ModeResult result;
    for (const ModeChange& change : changes) {
        const std::uint16_t now = before ? change.after : change.before;
        const std::uint16_t wanted = before ? change.before : change.after;
        struct stat info{};
        if (::stat(change.path.c_str(), &info) != 0) {
            result.errors.push_back(error_text(change.path, errno));
            continue;
        }
        if (!S_ISREG(info.st_mode) || (info.st_mode & 07777) != now) {
            result.errors.push_back(
                std::format("{}: changed since", change.path.filename().string()));
            continue;
        }
        if (::chmod(change.path.c_str(), wanted) != 0) {
            result.errors.push_back(error_text(change.path, errno));
            continue;
        }
        result.changed.push_back({.path = change.path, .before = now, .after = wanted});
    }
    return result;
}

}  // namespace nordend::fs
