#pragma once

#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace nordend::core {

struct TextFile {
    bool exists{false};  // false: no such file, which callers usually treat as "use defaults"
    std::string text;
    std::string error;  // set when the file exists but cannot be read; text is then empty
};

// Reads a whole regular file, opened once without waiting (a FIFO is refused, never waited on).
// A file larger than max_bytes is an error. Never throws for I/O problems: they land in error.
[[nodiscard]] TextFile read_text_file(
    const std::filesystem::path& path,
    std::size_t max_bytes = std::numeric_limits<std::size_t>::max());

struct WriteOptions {
    unsigned mode{0644};  // a new file's permissions; one already there keeps its own
    // The program's own file (state.toml): mode and a rwx------ folder even when they are there.
    bool private_file{false};
    // A symbolic link's target is written and the link stays (a dotfiles repository's); one that
    // leads nowhere, or into a read-only place (home-manager's), is an error. False: the link is
    // replaced by a file, for a file that must never write into what a link leads to.
    bool through_links{true};
};

// Replaces path's content with text, all or nothing: written to a hidden file beside it, synced,
// then renamed over it. Missing folders are made rwx------, as the XDG base directory spec asks.
// Returns what failed.
[[nodiscard]] std::error_code write_file_atomically(const std::filesystem::path& path,
                                                    std::string_view text,
                                                    const WriteOptions& options = {});

// The file:// URI of an absolute path, escaped exactly as GLib's g_filename_to_uri does (other
// programs compare these strings: thumbnails by their MD5, bookmarks, the clipboard).
[[nodiscard]] std::string file_uri(const std::filesystem::path& absolute);

// The local path a file URI names ("file:///a%20b", "file://localhost/a"), normalized; nullopt for
// anything else (another host, another scheme, a relative path, a NUL byte).
[[nodiscard]] std::optional<std::filesystem::path> path_from_uri(std::string_view uri);

}  // namespace nordend::core
