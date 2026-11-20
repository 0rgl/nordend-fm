#include "nordend/fs/scripts.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/core/event_loop.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/directory_watcher.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using nordend::core::EventLoop;
using nordend::core::WorkerPool;
using nordend::fs::find_scripts;
using nordend::fs::Script;
using nordend::fs::ScriptsFound;
using nordend::test::TempDir;
using namespace std::chrono_literals;

namespace {

using Names = std::vector<std::string>;

const fs::perms kFolder = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                          fs::perms::others_read | fs::perms::others_exec;  // 0755, whatever umask

// A file of the given mode; the folders made for it 0755.
fs::path file(const fs::path& path, fs::perms mode) {
    std::vector<fs::path> made;
    for (fs::path folder = path.parent_path(); !fs::exists(folder); folder = folder.parent_path()) {
        made.push_back(folder);
    }
    fs::create_directories(path.parent_path());
    for (const fs::path& folder : made) {
        fs::permissions(folder, kFolder);
    }
    std::ofstream{path} << "#!/bin/sh\n";
    fs::permissions(path, mode);
    return path;
}

const fs::perms kScript = fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                          fs::perms::others_read | fs::perms::others_exec;  // 0755
const fs::perms kNote = fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                        fs::perms::others_read;  // 0644

Names labels(const ScriptsFound& found) {
    Names names;
    for (const Script& script : found.scripts) {
        names.push_back(script.label);
    }
    return names;
}

// Whether a problem names this path.
bool said(const ScriptsFound& found, const fs::path& path) {
    return std::ranges::any_of(found.problems, [&](const std::string& problem) {
        return problem.find(path.string() + ":") != std::string::npos;
    });
}

bool run_until(EventLoop& loop, const std::function<bool()>& done) {
    bool ok = false;
    const auto poll = loop.add_timer(1ms, 1ms, [&] {
        if (done()) {
            ok = true;
            loop.quit(0);
        }
    });
    const auto watchdog = loop.add_timer(5s, 0ms, [&loop] { loop.quit(1); });
    loop.run();
    loop.remove(poll);
    loop.remove(watchdog);
    return ok;
}

}  // namespace

TEST_CASE("scripts: the user's executables, subfolders in the label, notes and hidden left out") {
    const TempDir dir;
    const fs::path scripts = dir.path / "scripts";
    fs::create_directory(scripts);
    fs::permissions(scripts, fs::perms::owner_all);
    file(scripts / "resize", kScript);
    file(scripts / "notes.txt", kNote);          // not executable: not a script, not a problem
    file(scripts / ".hidden", kScript);          // hidden: left out
    file(scripts / "Photos" / "crop", kScript);  // a subfolder: "Photos › crop"
    fs::permissions(scripts / "Photos", kScript);
    file(scripts / "Photos" / "2 two", kScript);
    file(scripts / "Photos" / "10 ten", kScript);  // natural order: 2 before 10

    const ScriptsFound found = find_scripts(scripts);
    CHECK(labels(found) == Names{"Photos › 2 two", "Photos › 10 ten", "Photos › crop", "resize"});
    CHECK(found.scripts.back().path == scripts / "resize");
    CHECK(found.problems.empty());
    CHECK(found.folders == std::vector<fs::path>{scripts, scripts / "Photos"});

    // No folder at all: nothing, and nothing wrong.
    const ScriptsFound none = find_scripts(dir.path / "missing");
    CHECK(none.scripts.empty());
    CHECK(none.problems.empty());
}

TEST_CASE("scripts: what others may write to is left out and said; two levels of folders") {
    const TempDir dir;
    const fs::path scripts = dir.path / "scripts";
    fs::create_directory(scripts);
    fs::permissions(scripts, fs::perms::owner_all);
    file(scripts / "mine", kScript);
    const fs::path shared = file(scripts / "shared", kScript | fs::perms::group_write);
    const fs::path open = scripts / "Open";
    file(open / "inside", kScript);
    fs::permissions(open, fs::perms::all);  // 0777: anyone may put a program there
    file(scripts / "A" / "B" / "fine", kScript);
    file(scripts / "A" / "B" / "C" / "too-deep", kScript);

    const ScriptsFound found = find_scripts(scripts);
    CHECK(labels(found) == Names{"A › B › fine", "mine"});
    CHECK(said(found, shared));
    CHECK(said(found, open));
    CHECK(said(found, scripts / "A" / "B" / "C"));

    // The folder itself open to others: nothing at all.
    fs::permissions(scripts, fs::perms::all);
    const ScriptsFound refused = find_scripts(scripts);
    CHECK(refused.scripts.empty());
    CHECK(said(refused, scripts));
}

TEST_CASE("scripts: links followed only to what passes the same checks") {
    const TempDir dir;
    const TempDir elsewhere;  // another folder of the user's, as Nautilus's scripts would be
    const fs::path scripts = dir.path / "scripts";
    fs::create_directory(scripts);
    fs::permissions(scripts, fs::perms::owner_all);
    const fs::path resize = file(scripts / "resize", kScript);
    fs::create_symlink(resize, scripts / "inside");
    fs::create_symlink(file(elsewhere.path / "theirs", kScript), scripts / "from-nautilus");
    const fs::path open = elsewhere.path / "open";
    fs::create_directory(open);
    fs::create_symlink(file(open / "planted", kScript), scripts / "planted");
    fs::permissions(open, fs::perms::all);  // the link leads into a folder anyone writes to
    fs::create_symlink(elsewhere.path / "gone", scripts / "dangling");
    fs::create_directory_symlink(elsewhere.path, scripts / "Folder");  // folders: not followed

    const ScriptsFound found = find_scripts(scripts);
    CHECK(labels(found) == Names{"from-nautilus", "inside", "resize"});
    CHECK(found.scripts.front().path == scripts / "from-nautilus");  // the link, not its target
    CHECK(said(found, scripts / "planted"));
    CHECK(said(found, scripts / "dangling"));
    CHECK_FALSE(said(found, scripts / "Folder"));

    // The folder itself a link to Nautilus's scripts, in a folder of the user's: followed. Into
    // a folder anyone writes to (here /tmp, where the test's folders are): not.
    const fs::path nautilus = file(elsewhere.path / "nautilus-scripts" / "theirs", kScript);
    const fs::path linked = dir.path / "linked";
    fs::create_directory_symlink(nautilus.parent_path(), linked);
    CHECK(labels(find_scripts(linked)) == Names{"theirs"});
    const fs::perms tmp = fs::status(elsewhere.path.parent_path()).permissions();
    if ((tmp & fs::perms::others_write) != fs::perms::none) {
        const fs::path into_tmp = dir.path / "into-tmp";
        fs::create_directory_symlink(elsewhere.path, into_tmp);
        const ScriptsFound refused = find_scripts(into_tmp);
        CHECK(refused.scripts.empty());
        CHECK(said(refused, into_tmp));
    }
}

TEST_CASE("scripts: at most kMaxScripts, the rest said") {
    const TempDir dir;
    const fs::path scripts = dir.path / "scripts";
    fs::create_directory(scripts);
    fs::permissions(scripts, fs::perms::owner_all);
    for (std::size_t i = 0; i < nordend::fs::kMaxScripts + 5; ++i) {
        file(scripts / ("s" + std::to_string(i)), kScript);
    }
    const ScriptsFound found = find_scripts(scripts);
    CHECK(found.scripts.size() == nordend::fs::kMaxScripts);
    CHECK(found.problems.size() == 1);
}

TEST_CASE("scripts: arguments and environment as Nautilus gives them") {
    const fs::path photos{"/home/me/My Photos"};
    const std::vector<fs::path> files{photos / "P1.ORF", photos / "sub/b c.jpg", "/elsewhere/x"};
    CHECK(nordend::fs::script_arguments(files, photos) ==
          Names{"P1.ORF", "sub/b c.jpg", "/elsewhere/x"});
    CHECK(nordend::fs::script_environment(files, photos) ==
          Names{"NAUTILUS_SCRIPT_SELECTED_FILE_PATHS=/home/me/My Photos/P1.ORF\n/home/me/My "
                "Photos/sub/b c.jpg\n/elsewhere/x\n",
                "NAUTILUS_SCRIPT_SELECTED_URIS=file:///home/me/My%20Photos/P1.ORF\nfile:///home/"
                "me/My%20Photos/sub/b%20c.jpg\nfile:///elsewhere/x\n",
                "NAUTILUS_SCRIPT_CURRENT_URI=file:///home/me/My%20Photos"});
    // The empty space: no files, the folder only.
    CHECK(nordend::fs::script_arguments({}, photos).empty());
    CHECK(nordend::fs::script_environment({}, photos).front() ==
          "NAUTILUS_SCRIPT_SELECTED_FILE_PATHS=");
}

TEST_CASE("scripts: followed live, the folder made later, a script added and removed") {
    const TempDir dir;
    const fs::path scripts = dir.path / "nordend" / "scripts";
    EventLoop loop;
    WorkerPool pool{loop, 2};
    nordend::fs::DirectoryWatcher watcher{loop};
    std::vector<ScriptsFound> seen;
    const nordend::fs::ScriptsWatcher follow{
        loop, pool, watcher, scripts, [&](const ScriptsFound& found) { seen.push_back(found); }};
    REQUIRE(run_until(loop, [&] { return !seen.empty(); }));
    CHECK(seen.back().scripts.empty());  // no folder yet

    fs::create_directory(scripts);
    fs::permissions(scripts, fs::perms::owner_all);
    file(scripts / "resize", kScript);
    REQUIRE(run_until(loop, [&] { return labels(seen.back()) == Names{"resize"}; }));
    file(scripts / "Photos" / "crop", kScript);
    fs::permissions(scripts / "Photos", kScript);
    REQUIRE(run_until(loop, [&] { return labels(seen.back()).size() == 2; }));
    file(scripts / "Photos" / "denoise", kScript);  // inside a subfolder: watched too
    REQUIRE(run_until(loop, [&] { return labels(seen.back()).size() == 3; }));
    fs::remove(scripts / "resize");
    REQUIRE(run_until(
        loop, [&] { return labels(seen.back()) == Names{"Photos › crop", "Photos › denoise"}; }));
}
