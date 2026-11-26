#include "nordend/core/file_watcher.hpp"

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/resource.h>
#include <system_error>
#include <unistd.h>

#include <doctest/doctest.h>

#include "nordend/core/event_loop.hpp"
#include "nordend/fs/directory_watcher.hpp"
#include "temp_dir.hpp"

using nordend::core::EventLoop;
using nordend::core::FileWatcher;
using nordend::test::TempDir;
using namespace std::chrono_literals;

namespace {

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif

// Runs the loop for a fixed time: long enough for inotify and the settle timer.
void run_for(EventLoop& loop, std::chrono::milliseconds time) {
    loop.add_timer(time, 0ms, [&loop] { loop.quit(0); });
    loop.run();
}

// The process's inotify instances, as /proc/self/fd shows them.
int inotify_instances() {
    int count = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator{"/proc/self/fd", error}) {
        std::error_code ignored;
        if (std::filesystem::read_symlink(entry.path(), ignored).string() == "anon_inode:inotify") {
            ++count;
        }
    }
    return count;
}

// While it lives, no new descriptor can be opened (RLIMIT_NOFILE at the lowest free one), so
// inotify_init1 fails with EMFILE, as when the user's inotify instances are all taken.
class NoDescriptorsLeft {
public:
    NoDescriptorsLeft() {
        REQUIRE(::getrlimit(RLIMIT_NOFILE, &saved_) == 0);
        const int lowest = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        REQUIRE(lowest >= 0);
        ::close(lowest);
        rlimit tight = saved_;
        tight.rlim_cur = static_cast<rlim_t>(lowest);
        REQUIRE(::setrlimit(RLIMIT_NOFILE, &tight) == 0);
    }

    ~NoDescriptorsLeft() { ::setrlimit(RLIMIT_NOFILE, &saved_); }

    NoDescriptorsLeft(const NoDescriptorsLeft&) = delete;
    NoDescriptorsLeft& operator=(const NoDescriptorsLeft&) = delete;
    NoDescriptorsLeft(NoDescriptorsLeft&&) = delete;
    NoDescriptorsLeft& operator=(NoDescriptorsLeft&&) = delete;

private:
    rlimit saved_{};
};

}  // namespace

TEST_CASE("file watcher: write, other files, coalescing, rename, removal") {
    const TempDir dir;
    const std::filesystem::path file = dir.path / "colors.toml";
    EventLoop loop;
    int calls = 0;
    const FileWatcher watcher{loop, file, [&] { ++calls; }, 20ms};

    SUBCASE("several writes in a row are one change") {
        for (int i = 0; i < 5; ++i) {
            dir.write("colors.toml", "x");
        }
        run_for(loop, 150ms);
        CHECK(calls == 1);
    }
    SUBCASE("other files in the directory are ignored") {
        dir.write("config.toml", "x");
        dir.write("colors.toml.tmp", "x");
        run_for(loop, 150ms);
        CHECK(calls == 0);
    }
    SUBCASE("an atomic replacement by rename is seen") {
        const std::filesystem::path temp = dir.write("colors.toml.tmp", "x");
        run_for(loop, 100ms);
        std::filesystem::rename(temp, file);
        run_for(loop, 150ms);
        CHECK(calls == 1);
    }
    SUBCASE("removal is seen, and later changes still are") {
        dir.write("colors.toml", "x");
        run_for(loop, 150ms);
        std::filesystem::remove(file);
        run_for(loop, 150ms);
        dir.write("colors.toml", "y");
        run_for(loop, 150ms);
        CHECK(calls == 3);
    }
}

TEST_CASE("file watcher creates a missing directory") {
    const TempDir dir;
    const std::filesystem::path file = dir.path / "nordend" / "colors.toml";
    EventLoop loop;
    int calls = 0;
    const FileWatcher watcher{loop, file, [&] { ++calls; }, 20ms};
    CHECK(std::filesystem::is_directory(file.parent_path()));
    std::ofstream{file} << "x";
    run_for(loop, 150ms);
    CHECK(calls == 1);
}

TEST_CASE("file watcher: another program's directory is never created") {
    const TempDir dir;
    const std::filesystem::path file = dir.path / "gtk-4.0" / "gtk.css";
    EventLoop loop;
    CHECK_THROWS_AS(
        FileWatcher(loop, file, [] {}, 20ms, FileWatcher::Directory::existing), std::system_error);
    CHECK_FALSE(std::filesystem::exists(file.parent_path()));
}

TEST_CASE("file watchers of one loop share one inotify instance (#57)") {
    const TempDir dir;
    const int before = inotify_instances();
    EventLoop loop;
    int colors = 0;
    int config = 0;
    int other = 0;
    // Two files in one folder, one in another: one instance, two watches.
    const FileWatcher colors_watcher{loop, dir.path / "colors.toml", [&] { ++colors; }, 20ms};
    auto config_watcher =
        std::make_unique<FileWatcher>(loop, dir.path / "config.toml", [&] { ++config; }, 20ms);
    const FileWatcher other_watcher{loop, dir.path / "gtk" / "gtk.css", [&] { ++other; }, 20ms};
    CHECK(inotify_instances() == before + 1);

    // Each sees only its own file.
    dir.write("colors.toml", "x");
    run_for(loop, 150ms);
    CHECK(colors == 1);
    CHECK(config == 0);
    CHECK(other == 0);
    dir.write("config.toml", "x");
    dir.write("gtk/gtk.css", "x");
    run_for(loop, 150ms);
    CHECK(colors == 1);
    CHECK(config == 1);
    CHECK(other == 1);

    // One watcher gone, the other in its folder still hears its file.
    config_watcher.reset();
    dir.write("colors.toml", "y");
    dir.write("config.toml", "y");
    run_for(loop, 150ms);
    CHECK(colors == 2);
    CHECK(config == 1);

    // Another loop gets an instance of its own.
    {
        EventLoop second;
        const FileWatcher elsewhere{second, dir.path / "colors.toml", [] {}, 20ms};
        CHECK(inotify_instances() == before + 2);
    }
    CHECK(inotify_instances() == before + 1);
}

TEST_CASE("file watchers: the instance goes with the loop's last watcher") {
    const TempDir dir;
    const int before = inotify_instances();
    EventLoop loop;
    {
        const FileWatcher one{loop, dir.path / "a.toml", [] {}, 20ms};
        const FileWatcher two{loop, dir.path / "b.toml", [] {}, 20ms};
        CHECK(inotify_instances() == before + 1);
    }
    CHECK(inotify_instances() == before);
    int calls = 0;
    const FileWatcher again{loop, dir.path / "a.toml", [&] { ++calls; }, 20ms};
    CHECK(inotify_instances() == before + 1);
    dir.write("a.toml", "x");
    run_for(loop, 150ms);
    CHECK(calls == 1);
}

TEST_CASE("file watchers: one that joins a watched folder hears only what comes after it") {
    const TempDir dir;
    EventLoop loop;
    int first = 0;
    const FileWatcher watcher{loop, dir.path / "colors.toml", [&] { ++first; }, 20ms};
    dir.write("colors.toml", "x");  // queued, not read yet: the loop has not run
    int second = 0;
    const FileWatcher joined{loop, dir.path / "colors.toml", [&] { ++second; }, 20ms};
    run_for(loop, 150ms);
    CHECK(first == 1);
    CHECK(second == 0);
    dir.write("colors.toml", "y");
    run_for(loop, 150ms);
    CHECK(first == 2);
    CHECK(second == 1);
}

TEST_CASE("file watchers: a folder that goes lets its watch go, and a new one works") {
    const TempDir dir;
    EventLoop loop;
    std::filesystem::create_directories(dir.path / "gone");
    int calls = 0;
    {
        const FileWatcher watcher{loop, dir.path / "gone" / "a.toml", [] {}, 20ms};
        std::filesystem::remove_all(dir.path / "gone");
        run_for(loop, 100ms);  // IN_IGNORED read
    }
    const FileWatcher fresh{loop, dir.path / "b.toml", [&] { ++calls; }, 20ms};
    dir.write("b.toml", "x");
    run_for(loop, 150ms);
    CHECK(calls == 1);
}

// Not under the sanitizers: their runtimes need descriptors of their own (ASan's pipe() to probe
// memory), and with none left they stop the whole run.
TEST_CASE("watchers: with no inotify instance left, they are made and stay quiet (#57)" *
          doctest::skip(kSanitized)) {
    const TempDir dir;
    std::filesystem::create_directories(dir.path / "gtk-4.0");
    std::filesystem::create_directories(dir.path / "folder");
    dir.write("photos.zip", "PK");
    EventLoop loop;  // its own descriptors first
    int calls = 0;
    std::unique_ptr<FileWatcher> file;
    std::unique_ptr<FileWatcher> existing;
    std::unique_ptr<nordend::fs::DirectoryWatcher> folders;
    std::error_code on_folder;
    std::error_code on_archive;
    std::error_code on_missing;
    bool missing_throws = false;
    bool following = true;
    {
        const NoDescriptorsLeft full;
        file =
            std::make_unique<FileWatcher>(loop, dir.path / "colors.toml", [&] { ++calls; }, 20ms);
        existing = std::make_unique<FileWatcher>(
            loop, dir.path / "gtk-4.0" / "gtk.css", [&] { ++calls; }, 20ms,
            FileWatcher::Directory::existing);
        try {
            const FileWatcher missing{loop, dir.path / "gtk-3.0" / "gtk.css", [] {}, 20ms,
                                      FileWatcher::Directory::existing};
        } catch (const std::system_error&) {
            missing_throws = true;
        }
        folders = std::make_unique<nordend::fs::DirectoryWatcher>(loop);
        following = folders->following();
        const auto none = [](const auto& /*events*/) {};
        CHECK(folders->watch(dir.path / "folder", none, on_folder) == 0);
        CHECK(folders->watch(dir.path / "photos.zip", none, on_archive) == 0);
        CHECK(folders->watch(dir.path / "gone", none, on_missing) == 0);
    }
    CHECK_FALSE(following);
    // A folder that must exist and does not is still refused, as with a watch.
    CHECK(missing_throws);
    // What the kernel would have said about the path, then that nothing follows a folder: an
    // archive still opens as a folder (ENOTDIR), a missing one is an error.
    CHECK(on_folder == std::errc::too_many_files_open);
    CHECK(on_archive == std::errc::not_a_directory);
    CHECK(on_missing == std::errc::no_such_file_or_directory);
    // Quiet.
    dir.write("colors.toml", "x");
    dir.write("gtk-4.0/gtk.css", "x");
    run_for(loop, 100ms);
    CHECK(calls == 0);
    // An instance free again: the next watch takes it, and follows.
    std::error_code again;
    int events = 0;
    const auto id =
        folders->watch(dir.path / "folder", [&](const auto& /*events*/) { ++events; }, again);
    CHECK(id != 0);
    CHECK(folders->following());
    dir.write("folder/new.txt", "x");
    run_for(loop, 100ms);
    CHECK(events > 0);
    folders->unwatch(id);
}
