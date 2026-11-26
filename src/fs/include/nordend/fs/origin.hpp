#pragma once

// Where a file came from: user.xdg.origin.url, from the freedesktop list of common extended
// attributes (https://wiki.freedesktop.org/CommonExtendedAttributes/), "set on a file
// downloaded from a url". Browsers set it on a download; Nordend sets it on a program it
// extracts from an archive, so Enter asks before the program first runs, as it does for one
// without its execute bit (issue #65). Allowing the program to run removes it.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace nordend::fs {

inline constexpr const char* kOriginAttribute = "user.xdg.origin.url";

// What an extracted program carries when its archive came with no origin of its own: nothing
// about where the archive is.
inline constexpr std::string_view kExtractedOrigin = "nordend:extracted";

// The attribute's value on path (links followed), when it has one that fits a sane size.
[[nodiscard]] std::optional<std::string> origin_of(const std::filesystem::path& path);

// Whether the open file fd carries the attribute, whatever its value.
[[nodiscard]] bool has_origin(int fd) noexcept;

// Sets it on fd. False, with errno, when it cannot be written (a file system without user
// attributes: vfat, exFAT, some FUSE mounts).
[[nodiscard]] bool set_origin(int fd, std::string_view value) noexcept;

// Gives to what from has. True when from has none, or to now has it too.
[[nodiscard]] bool copy_origin(int from, int to) noexcept;

// Removes it from path (links followed) where it can: a file without it, or where it cannot be
// removed (someone else's, a read-only disk, a file that cannot be read), is left as it is, which
// origin_of() then shows. A read-only file of the user's own gets u+w for the moment; the error
// says it could not be given its mode back.
[[nodiscard]] std::error_code forget_origin(const std::filesystem::path& path);

}  // namespace nordend::fs
