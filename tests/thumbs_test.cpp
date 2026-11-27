#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/formats/image/load.hpp"
#include "nordend/picture/load.hpp"
#include "nordend/picture/png.hpp"
#include "nordend/thumbs/cache.hpp"
#include "nordend/thumbs/md5.hpp"
#include "temp_dir.hpp"

namespace thumbs = nordend::thumbs;
namespace fs = std::filesystem;
using nordend::test::TempDir;

namespace {

const fs::path kData = fs::path{NORDEND_TEST_DATA} / "image";

int mode_of(const fs::path& path) {
    struct stat info{};
    REQUIRE(::stat(path.c_str(), &info) == 0);
    return static_cast<int>(info.st_mode & 0777);
}

fs::path fail_marker(const fs::path& root, const fs::path& file) {
    return root / "fail" / "nordend-test" / (thumbs::md5_hex(thumbs::file_uri(file)) + ".png");
}

// What the fail marker of file says about why it failed; empty when it says nothing.
std::string marker_failure(const fs::path& marker) {
    std::ifstream in{marker, std::ios::binary};
    const std::vector<std::uint8_t> png{std::istreambuf_iterator<char>{in},
                                        std::istreambuf_iterator<char>{}};
    for (const auto& [key, value] : nordend::picture::png_text(png)) {
        if (key == "X-Nordend::Failure") {
            return value;
        }
    }
    return {};
}

// The formats' own loader, as tests/main.cpp sets it, put back when it goes.
struct LoaderBack {
    LoaderBack() = default;
    LoaderBack(const LoaderBack&) = delete;
    LoaderBack& operator=(const LoaderBack&) = delete;
    LoaderBack(LoaderBack&&) = delete;
    LoaderBack& operator=(LoaderBack&&) = delete;

    ~LoaderBack() {
        nordend::picture::set_loader([](const nordend::picture::LoadRequest& request) {
            return nordend::formats::image::load(request);
        });
    }
};

}  // namespace

TEST_CASE("thumbs: MD5 of the RFC 1321 test suite") {
    CHECK(thumbs::md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(thumbs::md5_hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(thumbs::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(thumbs::md5_hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(thumbs::md5_hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(
        thumbs::md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234"
                        "567890") == "57edf4a22be3c955ac49da2e2107b67a");
}

TEST_CASE("thumbs: file URIs are escaped exactly as GLib does") {
    // Expected values from `gio info` (GLib's g_filename_to_uri).
    CHECK(thumbs::file_uri("/d/plain.jpg") == "file:///d/plain.jpg");
    CHECK(thumbs::file_uri("/d/spa ce.jpg") == "file:///d/spa%20ce.jpg");
    CHECK(thumbs::file_uri("/d/semi;colon.jpg") == "file:///d/semi%3Bcolon.jpg");
    CHECK(thumbs::file_uri("/d/excl!amp&'()*+,=:@$~_-.jpg") ==
          "file:///d/excl!amp&'()*+,=:@$~_-.jpg");
    CHECK(thumbs::file_uri("/d/pct%hash#q?.jpg") == "file:///d/pct%25hash%23q%3F.jpg");
    CHECK(thumbs::file_uri("/d/accent\xc3\xb2\xe2\x82\xac.jpg") ==
          "file:///d/accent%C3%B2%E2%82%AC.jpg");
    CHECK(thumbs::file_uri("/d/brack[et]{x}^|`\"<>.jpg") ==
          "file:///d/brack%5Bet%5D%7Bx%7D%5E%7C%60%22%3C%3E.jpg");
    // The spec's own example: the thumbnail name is the MD5 of the URI.
    CHECK(thumbs::md5_hex("file:///home/jens/photos/me.png") == "c6ee772d9e49320e97ec29a7eb5b1697");
}

TEST_CASE("thumbs: sizes") {
    CHECK(thumbs::size_for(1) == thumbs::Size::normal);
    CHECK(thumbs::size_for(128) == thumbs::Size::normal);
    CHECK(thumbs::size_for(129) == thumbs::Size::large);
    CHECK(thumbs::size_for(384) == thumbs::Size::x_large);
    CHECK(thumbs::size_for(5000) == thumbs::Size::xx_large);
    CHECK(thumbs::directory_name(thumbs::Size::x_large) == "x-large");
}

TEST_CASE("thumbs: made once, then read from the cache until the file changes") {
    TempDir dir;
    const fs::path photo = dir.path / "photo one.jpg";
    fs::copy_file(kData / "with-thumbnail.jpg", photo);
    const thumbs::Cache cache{dir.path / "thumbnails", "nordend-test"};

    // 480x320 with a 192x128 EXIF thumbnail: enough for 128, so the thumbnail comes from it,
    // scaled to fit, then turned upright (orientation 6).
    const thumbs::Thumbnail made = cache.get(photo, thumbs::Size::normal);
    REQUIRE(made.source == thumbs::Source::preview);
    CHECK(made.image.width == 85);
    CHECK(made.image.height == 128);
    const fs::path stored = cache.path_for(photo, thumbs::Size::normal);
    CHECK(stored ==
          dir.path / "thumbnails" / "normal" / (thumbs::md5_hex(thumbs::file_uri(photo)) + ".png"));
    REQUIRE(fs::exists(stored));
    CHECK(mode_of(stored) == 0600);
    CHECK(mode_of(stored.parent_path()) == 0700);
    CHECK(mode_of(dir.path / "thumbnails") == 0700);

    const thumbs::Thumbnail cached = cache.get(photo, thumbs::Size::normal);
    CHECK(cached.source == thumbs::Source::cache);
    CHECK(cached.image.rgba == made.image.rgba);

    // Changed: the stored one no longer matches its Thumb::MTime.
    fs::last_write_time(photo, fs::last_write_time(photo) + std::chrono::seconds{10});
    CHECK(cache.get(photo, thumbs::Size::normal).source == thumbs::Source::preview);
    CHECK(cache.get(photo, thumbs::Size::normal).source == thumbs::Source::cache);

    // At 256 the embedded 192x128 is too small: the whole image, upright.
    const thumbs::Thumbnail large = cache.get(photo, thumbs::Size::large);
    CHECK(large.source == thumbs::Source::decoded);
    CHECK(large.image.width == 171);
    CHECK(large.image.height == 256);
}

TEST_CASE("thumbs: a failure is remembered until the file changes, make() tries again") {
    TempDir dir;
    const fs::path broken = dir.path / "broken.jpg";
    fs::copy_file(kData / "truncated.jpg", broken);
    const thumbs::Cache cache{dir.path / "thumbnails", "nordend-test"};

    const thumbs::Thumbnail first = cache.get(broken, thumbs::Size::large);
    CHECK(first.source == thumbs::Source::failed);
    CHECK(first.image.empty());
    CHECK(marker_failure(fail_marker(dir.path / "thumbnails", broken)) == "damaged");
    CHECK(cache.get(broken, thumbs::Size::large).error == "failed before, and unchanged since");
    CHECK(cache.make(broken, thumbs::Size::large).error != "failed before, and unchanged since");

    // Fixed: a good image under the same name.
    fs::copy_file(kData / "blocks.jpg", broken, fs::copy_options::overwrite_existing);
    fs::last_write_time(broken, fs::last_write_time(broken) + std::chrono::seconds{10});
    CHECK(cache.get(broken, thumbs::Size::large).source == thumbs::Source::decoded);

    CHECK(cache.get(dir.path / "missing.jpg", thumbs::Size::large).source ==
          thumbs::Source::failed);
}

TEST_CASE("thumbs: a failure that passes is not remembered: the next get() tries again") {
    TempDir dir;
    const fs::path photo = dir.path / "photo.jpg";
    fs::copy_file(kData / "blocks.jpg", photo);
    const thumbs::Cache cache{dir.path / "thumbnails", "nordend-test"};
    const LoaderBack back;
    // A helper that took too long once, then answers.
    std::atomic<int> asked{0};
    nordend::picture::set_loader([&asked](const nordend::picture::LoadRequest& request) {
        if (asked++ == 0) {
            nordend::picture::Loaded late;
            late.failure = nordend::picture::Failure::unavailable;
            late.detail = "the preview helper took longer than 3000 ms";
            return late;
        }
        return nordend::formats::image::load(request);
    });
    CHECK(cache.get(photo, thumbs::Size::large).source == thumbs::Source::failed);
    CHECK_FALSE(fs::exists(fail_marker(dir.path / "thumbnails", photo)));
    CHECK(cache.get(photo, thumbs::Size::large).source == thumbs::Source::decoded);
    CHECK(asked == 2);
}

TEST_CASE("thumbs: a file that cannot be read is not remembered, and comes once it can be") {
    if (::geteuid() == 0) {
        MESSAGE("root reads every file: skipped");
        return;
    }
    TempDir dir;
    const fs::path photo = dir.path / "photo.jpg";
    fs::copy_file(kData / "blocks.jpg", photo);
    const thumbs::Cache cache{dir.path / "thumbnails", "nordend-test"};
    REQUIRE(cache.get(photo, thumbs::Size::large).source == thumbs::Source::decoded);
    // chmod leaves the modification time as it was: the thumbnail made before still matches it.
    fs::permissions(photo, fs::perms::none);
    CHECK(cache.get(photo, thumbs::Size::large).source == thumbs::Source::failed);
    CHECK_FALSE(fs::exists(fail_marker(dir.path / "thumbnails", photo)));
    fs::permissions(photo, fs::perms::owner_read | fs::perms::owner_write);
    CHECK(cache.get(photo, thumbs::Size::large).source == thumbs::Source::cache);
}

TEST_CASE("thumbs: a fail marker that says no reason (an older Nordend's) is tried once more") {
    TempDir dir;
    const fs::path photo = dir.path / "photo.jpg";
    fs::copy_file(kData / "blocks.jpg", photo);
    const fs::path root = dir.path / "thumbnails";
    const thumbs::Cache cache{root, "nordend-test"};
    struct stat info{};
    REQUIRE(::stat(photo.c_str(), &info) == 0);
    const auto old_marker = [&](const fs::path& file) {
        const nordend::picture::Image blank{.width = 1, .height = 1, .rgba = {0, 0, 0, 0}};
        const std::vector<std::uint8_t> png =
            nordend::picture::encode_png(blank, {{"Thumb::URI", thumbs::file_uri(file)},
                                                 {"Thumb::MTime", std::to_string(info.st_mtime)},
                                                 {"Software", "Nordend"}});
        fs::create_directories(fail_marker(root, file).parent_path());
        std::ofstream{fail_marker(root, file), std::ios::binary}.write(
            reinterpret_cast<const char*>(png.data()),  // NOLINT: bytes to chars
            static_cast<std::streamsize>(png.size()));
    };
    old_marker(photo);
    CHECK(cache.get(photo, thumbs::Size::large).source == thumbs::Source::decoded);

    // A damaged file behind an old marker: tried, and its new marker says why.
    const fs::path broken = dir.path / "broken.jpg";
    fs::copy_file(kData / "truncated.jpg", broken);
    REQUIRE(::stat(broken.c_str(), &info) == 0);
    old_marker(broken);
    CHECK(cache.get(broken, thumbs::Size::large).error != "failed before, and unchanged since");
    CHECK(marker_failure(fail_marker(root, broken)) == "damaged");
    CHECK(cache.get(broken, thumbs::Size::large).error == "failed before, and unchanged since");
}

TEST_CASE("thumbs: the cache never thumbnails its own files") {
    TempDir dir;
    const thumbs::Cache cache{dir.path / "thumbnails", "nordend-test"};
    const fs::path photo = dir.path / "a.png";
    fs::copy_file(kData / "blocks.png", photo);
    REQUIRE(cache.get(photo, thumbs::Size::normal).source == thumbs::Source::decoded);
    const thumbs::Thumbnail own =
        cache.get(cache.path_for(photo, thumbs::Size::normal), thumbs::Size::normal);
    CHECK(own.source == thumbs::Source::failed);
}

namespace {

// A thumbnail of file in folder (under root), as any app writes one.
fs::path put_thumbnail(const fs::path& folder, const fs::path& file) {
    fs::create_directories(folder);
    const fs::path png = folder / (thumbs::md5_hex(thumbs::file_uri(file)) + ".png");
    const std::vector<std::uint8_t> data = nordend::picture::encode_png(
        {.width = 1, .height = 1, .rgba = {0, 0, 0, 255}},
        {{"Software", "Some viewer"}, {"Thumb::URI", thumbs::file_uri(file)}});
    std::ofstream{png, std::ios::binary}.write(
        reinterpret_cast<const char*>(data.data()),  // NOLINT: bytes to chars
        static_cast<std::streamsize>(data.size()));
    return png;
}

}  // namespace

TEST_CASE("thumbs: forget() takes every size and every Nordend's marker, not other apps'") {
    TempDir dir;
    const fs::path root = dir.path / "thumbnails";
    const thumbs::Cache cache{root, "nordend-test"};
    const fs::path gone = dir.path / "photos" / "a b.jpg";
    const fs::path kept = dir.path / "photos" / "c.jpg";
    std::vector<fs::path> forgotten;
    for (const char* folder :
         {"normal", "large", "x-large", "xx-large", "fail/nordend-test", "fail/nordend-0.10.1"}) {
        forgotten.push_back(put_thumbnail(root / folder, gone));
    }
    const fs::path theirs = put_thumbnail(root / "fail" / "gnome-thumbnail-factory", gone);
    const fs::path other = put_thumbnail(root / "normal", kept);

    const std::vector<fs::path> files{gone};
    cache.forget(files);
    for (const fs::path& png : forgotten) {
        CHECK_FALSE(fs::exists(png));
    }
    CHECK(fs::exists(theirs));
    CHECK(fs::exists(other));
}

TEST_CASE("thumbs: forget_inside() takes what was in a folder, found by Thumb::URI") {
    TempDir dir;
    const fs::path root = dir.path / "thumbnails";
    const thumbs::Cache cache{root, "nordend-test"};
    const fs::path trip = dir.path / "Trip";
    const fs::path in_trip = put_thumbnail(root / "large", trip / "P1.ORF");
    const fs::path deeper = put_thumbnail(root / "normal", trip / "day 2" / "P2.ORF");
    const fs::path marker = put_thumbnail(root / "fail" / "nordend-test", trip / "broken.jpg");
    const fs::path theirs = put_thumbnail(root / "fail" / "gnome-thumbnail-factory", trip / "x");
    const fs::path sibling = put_thumbnail(root / "normal", dir.path / "Trip 2" / "P3.ORF");
    const fs::path itself = put_thumbnail(root / "normal", trip);
    std::ofstream{root / "normal" / "notes.txt"} << "not a thumbnail";
    std::ofstream{root / "normal" / "broken.png"} << "\x89PNG\r\n\x1a\n";
    REQUIRE(::mkfifo((root / "normal" / "pipe.png").c_str(), 0600) == 0);  // never opened to wait

    const std::vector<fs::path> folders{trip};
    cache.forget_inside(folders);
    CHECK_FALSE(fs::exists(in_trip));
    CHECK_FALSE(fs::exists(deeper));
    CHECK_FALSE(fs::exists(marker));
    CHECK(fs::exists(theirs));
    CHECK(fs::exists(sibling));  // "Trip 2" only starts like "Trip"
    CHECK(fs::exists(itself));   // the folder's own: forget() takes it
    CHECK(fs::exists(root / "normal" / "notes.txt"));
    CHECK(fs::exists(root / "normal" / "broken.png"));
}
