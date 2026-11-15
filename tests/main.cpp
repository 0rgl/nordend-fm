#define DOCTEST_CONFIG_IMPLEMENT
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/archive/archive.hpp"
#include "nordend/engines/engines.hpp"
#include "nordend/formats/archive/archive.hpp"
#include "nordend/formats/image/load.hpp"
#include "nordend/formats/image/photo.hpp"
#include "nordend/formats/text/read.hpp"
#include "nordend/fs/trash.hpp"
#include "nordend/picture/load.hpp"
#include "nordend/text/text.hpp"

int main(int argc, char** argv) {
    // No test reads or makes the user's trash: the trash folder lists one of its own, and no
    // disk's.
    std::string root = (std::filesystem::temp_directory_path() / "nordend-tests-XXXXXX").string();
    if (mkdtemp(root.data()) == nullptr) {
        return 1;
    }
    nordend::fs::set_trash_sources({.home = std::filesystem::path{root} / "Trash",
                                    .tops = std::vector<std::filesystem::path>{},
                                    .uid = geteuid()});
    // Nor the user's configuration, data, cache or state: a key that writes there (Ctrl+D adds a
    // bookmark to gtk-3.0/bookmarks) writes into the test root. Set before any thread starts.
    for (const auto& [name, folder] :
         {std::pair{"XDG_CONFIG_HOME", "config"}, std::pair{"XDG_DATA_HOME", "data"},
          std::pair{"XDG_CACHE_HOME", "cache"}, std::pair{"XDG_STATE_HOME", "state"}}) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): no other thread yet
        setenv(name, (std::filesystem::path{root} / folder).c_str(), 1);
    }
    // The MIME database found at build time (tests/CMakeLists.txt), the same on every system,
    // whatever $XDG_DATA_DIRS the tests run with.
    // NOLINTNEXTLINE(concurrency-mt-unsafe): no other thread yet
    setenv("XDG_DATA_DIRS", NORDEND_MIME_DATA, 1);
    // The engines Nordend ships, from their manifests in the source tree (their programs are not
    // started here).
    nordend::engines::set_installed(std::make_shared<const nordend::engines::Registry>(
        nordend::engines::Registry::load(NORDEND_ENGINES, [](std::string_view program) {
            return std::filesystem::path{program};
        })));
    // Pictures made in this process, by the image family itself (Nordend sends them to its
    // isolated helpers instead; tests/helpers_test.cpp covers that path).
    nordend::picture::set_loader([](const nordend::picture::LoadRequest& request) {
        return nordend::formats::image::load(request);
    });
    nordend::picture::set_info_loader([](const std::filesystem::path& path) {
        return nordend::formats::image::read_photo_info(path);
    });
    nordend::text::set_loader([](const nordend::text::LoadRequest& request) {
        return nordend::formats::text::load(request);
    });
    nordend::archive::set_lister(
        [](const std::filesystem::path& path, const nordend::archive::Limits& limits) {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                return nordend::archive::Listing{.format = nordend::archive::Format::unknown,
                                                 .failure = nordend::archive::Failure::unreadable,
                                                 .detail = "it could not be opened",
                                                 .entries = {}};
            }
            nordend::archive::Listing listing = nordend::formats::archive::list(fd, limits);
            ::close(fd);
            return listing;
        });
    nordend::archive::set_entry_reader(
        [](const std::filesystem::path& path, nordend::archive::Format format,
           const nordend::archive::Entry& entry, int out, std::uint64_t max_bytes) {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                return nordend::archive::Read{.failure = nordend::archive::Failure::unreadable,
                                              .detail = "it could not be opened",
                                              .bytes = 0};
            }
            nordend::archive::Read read =
                nordend::formats::archive::read_entry(fd, format, entry, out, max_bytes);
            ::close(fd);
            return read;
        });
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const int result = context.run();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return result;
}
