#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "nordend/core/event_loop.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/directory_watcher.hpp"

// Scripts of the user's own in the context menu (issue #26), as Nautilus has them: every program
// in a folder, no configuration, a layer over [[action]] rather than a second system.
//
// Safety first. A folder is read only when it belongs to the user and nobody else may write to
// it, and a script is listed only when it is a regular file of the user's, executable, that
// nobody else may write to. A link is followed only to a file (or, for the folder itself, to a
// folder) that passes the same checks, in a folder that does too: a link to Nautilus's scripts
// works, one into /tmp does not. Hidden names are left out; two levels of subfolders at most, and
// kMaxScripts scripts. Nothing here runs anything.
namespace nordend::fs {

inline constexpr std::size_t kMaxScripts = 200;
inline constexpr int kScriptLevels = 3;  // the folder and two levels of subfolders

struct Script {
    std::string label;           // "resize", or "Photos › resize" from a subfolder
    std::filesystem::path path;  // absolute; the link itself when the script is a link

    bool operator==(const Script&) const = default;
};

struct ScriptsFound {
    std::vector<Script> scripts;  // each level in natural order, a subfolder's at its place
    std::vector<std::filesystem::path> folders;  // every folder read, to watch for changes
    std::vector<std::string> problems;           // one line each, ready to print
};

// $XDG_DATA_HOME/nordend/scripts, else ~/.local/share/nordend/scripts; empty when neither
// variable is set.
[[nodiscard]] std::filesystem::path default_scripts_folder();

// Every script under folder. Blocking: call it from a worker. A missing folder is no problem.
[[nodiscard]] ScriptsFound find_scripts(const std::filesystem::path& folder);

// What Nautilus gives a script it runs (its source, src/nautilus-files-view.c): the selected
// files as arguments, each relative to the folder shown (absolute when it is not under it).
[[nodiscard]] std::vector<std::string> script_arguments(
    std::span<const std::filesystem::path> files, const std::filesystem::path& directory);

// ...and the environment: NAUTILUS_SCRIPT_SELECTED_FILE_PATHS and _URIS (one a line, each line
// ended), NAUTILUS_SCRIPT_CURRENT_URI (the folder shown), as "NAME=value".
[[nodiscard]] std::vector<std::string> script_environment(
    std::span<const std::filesystem::path> files, const std::filesystem::path& directory);

// Keeps the scripts under a folder known: read on a worker at once, and again whenever the
// folder appears, goes away, or anything in it or in its subfolders changes (inotify, settled).
// The handler runs on the loop thread with what was found.
//
// Loop thread only. Must not outlive the loop, the pool or the watcher.
class ScriptsWatcher {
public:
    using Handler = std::function<void(const ScriptsFound&)>;

    static constexpr std::chrono::milliseconds kSettle{30};

    ScriptsWatcher(core::EventLoop& loop, core::WorkerPool& pool, DirectoryWatcher& watcher,
                   std::filesystem::path folder, Handler handler);
    ~ScriptsWatcher();

    ScriptsWatcher(const ScriptsWatcher&) = delete;
    ScriptsWatcher& operator=(const ScriptsWatcher&) = delete;
    ScriptsWatcher(ScriptsWatcher&&) = delete;
    ScriptsWatcher& operator=(ScriptsWatcher&&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nordend::fs
