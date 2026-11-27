#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nordend/core/file.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/picture/image.hpp"

namespace nordend::thumbs {

// The sizes of the freedesktop thumbnail spec (0.9): a thumbnail fits in a square of that side.
enum class Size : std::uint8_t { normal, large, x_large, xx_large };

[[nodiscard]] std::uint32_t pixels(Size size) noexcept;  // 128, 256, 512, 1024

[[nodiscard]] std::string_view directory_name(Size size) noexcept;  // "normal", "large", ...

// The smallest size whose side is at least side pixels; xx_large when none is.
[[nodiscard]] Size size_for(std::uint32_t side) noexcept;

// The file:// URI of a path as GLib writes it: other apps find the thumbnail by the MD5 of this
// string, so it must match byte for byte.
using core::file_uri;

// $XDG_CACHE_HOME/thumbnails, or ~/.cache/thumbnails. Reads the environment: call it at startup.
[[nodiscard]] std::filesystem::path default_root();

// Where a thumbnail comes from.
enum class Source : std::uint8_t {
    cache,    // a valid thumbnail was already there (ours or another app's)
    preview,  // made from a preview embedded in the file
    decoded,  // made from the whole image
    failed,   // could not be made (now, or remembered as failed)
};

[[nodiscard]] std::string_view source_name(Source source) noexcept;

struct Thumbnail {
    picture::Image image;  // upright, fits in pixels(size); empty when failed
    Source source{Source::failed};
    std::string error;  // why it failed
};

// The freedesktop thumbnail cache (~/.cache/thumbnails), shared with the other apps: a
// thumbnail is a PNG named after the MD5 of the file's URI, valid while its Thumb::URI and
// Thumb::MTime match the file. Failures are remembered in fail/<app>/ so that a file that cannot
// be read is not tried again until it changes.
//
// Thread-safe, meant to be called from workers.
class Cache {
public:
    // app names the failure directory, e.g. "nordend-0.0.1".
    Cache(std::filesystem::path root, std::string app);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    // Where the thumbnail of file (an absolute path) lives, valid or not.
    [[nodiscard]] std::filesystem::path path_for(const std::filesystem::path& file,
                                                 Size size) const;

    // The thumbnail of file at size: the cached one when valid, else made from the file and
    // stored. A failure that lasts (an unknown format, a damaged file) is remembered until the
    // file changes; one that passes (a helper that took too long) and a file that cannot be read
    // are not. Blocking (reads, decodes, writes): call it on a worker.
    [[nodiscard]] Thumbnail get(const std::filesystem::path& file, Size size) const;

    // Like get(), but never from the cache, and a remembered failure is tried again.
    [[nodiscard]] Thumbnail make(const std::filesystem::path& file, Size size) const;

    // Files inside these folders, through links too, still get thumbnails from get() and make(),
    // but nothing of them is read from the cache nor written to it, failures included. Any
    // thread; the links are resolved later, on a worker.
    void set_skipped(std::span<const std::filesystem::path> folders);
    [[nodiscard]] bool skipped(const std::filesystem::path& file) const;

    // Removes the thumbnails of files that are gone or moved: every size, whoever made them, and
    // the failure markers of every Nordend; other apps' markers stay. Blocking: on a worker.
    void forget(std::span<const std::filesystem::path> files) const;

    // The same for every thumbnail of a file inside one of folders, found by its Thumb::URI: one
    // pass over the cache that reads each PNG only up to its image data. Stops when cancelled.
    void forget_inside(std::span<const std::filesystem::path> folders,
                       const core::CancelToken& token = {}) const;

private:
    [[nodiscard]] std::filesystem::path fail_path(const std::filesystem::path& file) const;
    [[nodiscard]] std::vector<std::filesystem::path> forgettable_folders() const;
    [[nodiscard]] Thumbnail make(const std::filesystem::path& file, Size size, bool kept,
                                 std::int64_t mtime) const;

    std::filesystem::path root_;
    std::string app_;

    struct Skipped {
        std::vector<std::string> folders;  // normal, no slash at the end
        std::once_flag resolving;
        std::vector<std::string> real;  // the same with links resolved, once one is asked about
    };

    mutable std::mutex skipped_mutex_;
    std::shared_ptr<Skipped> skipped_;
};

}  // namespace nordend::thumbs
