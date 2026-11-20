#include "nordend/fs/scripts.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <format>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "nordend/core/file.hpp"
#include "nordend/core/file_watcher.hpp"
#include "nordend/core/log.hpp"
#include "nordend/fs/sort.hpp"

namespace nordend::fs {
namespace {

// The user's (or root's: a script of the system's, only root writes it) and written by nobody
// else: a folder or a file it is safe to take programs from.
bool trusted(const struct stat& info, uid_t user) {
    return (info.st_uid == user || info.st_uid == 0) && (info.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

std::string untrusted_why(const struct stat& info, uid_t user) {
    if (info.st_uid != user && info.st_uid != 0) {
        return "belongs to someone else";
    }
    return "others may write to it (chmod go-w)";
}

// A descriptor closed when it goes.
class Fd {
public:
    explicit Fd(int fd) : fd_{fd} {}

    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&&) = delete;
    Fd& operator=(Fd&&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_;
};

// The names in a folder open as fd, hidden ones left out, in natural order.
std::vector<std::string> names_in(int fd) {
    std::vector<std::string> names;
    const int copy = ::dup(fd);  // closedir closes the descriptor it is given
    if (copy < 0) {
        return names;
    }
    DIR* const dir = ::fdopendir(copy);
    if (dir == nullptr) {
        ::close(copy);
        return names;
    }
    while (const dirent* const entry = ::readdir(dir)) {  // NOLINT(concurrency-mt-unsafe)
        const std::string_view name{entry->d_name};
        if (!name.empty() && name.front() != '.') {
            names.emplace_back(name);
        }
    }
    ::closedir(dir);
    std::ranges::sort(names, [](const std::string& a, const std::string& b) {
        return natural_compare(a, b) < 0;
    });
    return names;
}

// A link followed: what it leads to must be of kind (S_IFREG or S_IFDIR) and trusted, in a
// folder that is trusted too. Empty when it may be used, else why not.
std::string link_problem(const std::filesystem::path& link, mode_t kind, uid_t user) {
    std::error_code error;
    const std::filesystem::path target = std::filesystem::canonical(link, error);
    if (error) {
        return "a link that leads nowhere";
    }
    struct stat info{};
    if (::stat(target.c_str(), &info) != 0 || (info.st_mode & S_IFMT) != kind) {
        return kind == S_IFDIR ? "a link to something that is not a folder"
                               : "a link to something that is not a file";
    }
    if (!trusted(info, user)) {
        return std::format("a link to {}, which {}", target.string(), untrusted_why(info, user));
    }
    struct stat parent{};
    if (::stat(target.parent_path().c_str(), &parent) != 0 || !trusted(parent, user)) {
        return std::format("a link into {}, which others may write to",
                           target.parent_path().string());
    }
    return {};
}

struct Reader {
    uid_t user{::geteuid()};
    ScriptsFound found;
    bool full{false};

    void add(std::string label, std::filesystem::path path) {
        if (found.scripts.size() == kMaxScripts) {
            if (!full) {
                found.problems.push_back(
                    std::format("scripts: more than {}, the rest left out", kMaxScripts));
                full = true;
            }
            return;
        }
        found.scripts.push_back({.label = std::move(label), .path = std::move(path)});
    }

    void problem(const std::filesystem::path& path, std::string_view why) {
        found.problems.push_back(std::format("scripts: {}: {}, left out", path.string(), why));
    }

    // A trusted folder open as fd, shown as path; its names labelled after prefix. Recursive,
    // kScriptLevels deep at most.
    void read(  // NOLINT(misc-no-recursion)
        int fd, const std::filesystem::path& path, const std::string& prefix, int level) {
        found.folders.push_back(path);
        for (const std::string& name : names_in(fd)) {
            const std::filesystem::path full_path = path / name;
            struct stat info{};
            if (::fstatat(fd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
                continue;
            }
            if (S_ISDIR(info.st_mode)) {
                if (level + 1 >= kScriptLevels) {
                    problem(full_path, "deeper than two levels of folders");
                    continue;
                }
                const Fd sub{
                    ::openat(fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
                struct stat opened{};
                if (sub.get() < 0 || ::fstat(sub.get(), &opened) != 0) {
                    continue;
                }
                if (!trusted(opened, user)) {
                    problem(full_path, untrusted_why(opened, user));
                    continue;
                }
                read(sub.get(), full_path, prefix + name + " › ", level + 1);
            } else if (S_ISREG(info.st_mode)) {
                if ((info.st_mode & S_IXUSR) == 0) {
                    continue;  // a note or a README beside the scripts: not one
                }
                if (!trusted(info, user)) {
                    problem(full_path, untrusted_why(info, user));
                    continue;
                }
                add(prefix + name, full_path);
            } else if (S_ISLNK(info.st_mode)) {
                struct stat target{};
                if (::stat(full_path.c_str(), &target) == 0 && S_ISDIR(target.st_mode)) {
                    continue;  // links to folders are not followed (no loops)
                }
                if (::stat(full_path.c_str(), &target) == 0 && (target.st_mode & S_IXUSR) == 0) {
                    continue;
                }
                if (const std::string why = link_problem(full_path, S_IFREG, user); !why.empty()) {
                    problem(full_path, why);
                    continue;
                }
                add(prefix + name, full_path);
            }
        }
    }
};

}  // namespace

std::filesystem::path default_scripts_folder() {
    // NOLINTBEGIN(concurrency-mt-unsafe): read on the loop thread, before any setenv
    const char* const data = std::getenv("XDG_DATA_HOME");
    if (data != nullptr && *data == '/') {
        return std::filesystem::path{data} / "nordend/scripts";
    }
    const char* const home = std::getenv("HOME");
    // NOLINTEND(concurrency-mt-unsafe)
    if (home != nullptr && *home == '/') {
        return std::filesystem::path{home} / ".local/share/nordend/scripts";
    }
    return {};
}

ScriptsFound find_scripts(const std::filesystem::path& folder) {
    Reader reader;
    struct stat link{};
    if (folder.empty() || ::lstat(folder.c_str(), &link) != 0) {
        return reader.found;  // no scripts folder: nothing to show, nothing wrong
    }
    if (S_ISLNK(link.st_mode)) {
        if (const std::string why = link_problem(folder, S_IFDIR, reader.user); !why.empty()) {
            reader.problem(folder, why);
            return reader.found;
        }
    }
    const Fd fd{::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    struct stat info{};
    if (fd.get() < 0 || ::fstat(fd.get(), &info) != 0) {
        reader.problem(folder, std::error_code{errno, std::generic_category()}.message());
        return reader.found;
    }
    if (!trusted(info, reader.user)) {
        reader.problem(folder, untrusted_why(info, reader.user));
        return reader.found;
    }
    reader.read(fd.get(), folder, {}, 0);
    return reader.found;
}

std::vector<std::string> script_arguments(std::span<const std::filesystem::path> files,
                                          const std::filesystem::path& directory) {
    std::vector<std::string> arguments;
    for (const std::filesystem::path& file : files) {
        const std::filesystem::path relative = file.lexically_relative(directory);
        const bool under = !relative.empty() && *relative.begin() != "..";
        arguments.push_back(under ? relative.string() : file.string());
    }
    return arguments;
}

std::vector<std::string> script_environment(std::span<const std::filesystem::path> files,
                                            const std::filesystem::path& directory) {
    std::string paths;
    std::string uris;
    for (const std::filesystem::path& file : files) {
        paths += file.string() + '\n';
        uris += core::file_uri(file) + '\n';
    }
    return {"NAUTILUS_SCRIPT_SELECTED_FILE_PATHS=" + paths, "NAUTILUS_SCRIPT_SELECTED_URIS=" + uris,
            "NAUTILUS_SCRIPT_CURRENT_URI=" + core::file_uri(directory)};
}

struct ScriptsWatcher::Impl {
    core::EventLoop& loop;
    core::WorkerPool& pool;
    DirectoryWatcher& watcher;
    std::filesystem::path folder;
    Handler handler;
    std::unique_ptr<core::FileWatcher> self;  // the folder itself appearing or going
    std::vector<DirectoryWatcher::WatchId> watches;
    core::EventLoop::SourceId timer{0};
    core::CancelToken reading;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    Impl(core::EventLoop& l, core::WorkerPool& p, DirectoryWatcher& w, std::filesystem::path f,
         Handler h)
        : loop{l}, pool{p}, watcher{w}, folder{std::move(f)}, handler{std::move(h)} {}

    // Something changed: read again once things are quiet.
    void schedule() {
        if (timer != 0) {
            loop.remove(timer);
        }
        timer = loop.add_timer(kSettle, std::chrono::milliseconds{0}, [this] {
            timer = 0;
            read();
        });
    }

    void read() {
        reading.cancel();
        reading = pool.submit(
            core::Priority::nearby,
            [path = folder](const core::CancelToken& /*token*/) { return find_scripts(path); },
            [this, living = alive](const ScriptsFound& found) {
                if (!*living) {
                    return;
                }
                rewatch(found.folders);
                handler(found);
            });
    }

    void rewatch(const std::vector<std::filesystem::path>& folders) {
        for (const DirectoryWatcher::WatchId id : watches) {
            watcher.unwatch(id);
        }
        watches.clear();
        for (const std::filesystem::path& path : folders) {
            std::error_code error;
            const DirectoryWatcher::WatchId id = watcher.watch(
                path, [this](const std::vector<DirectoryEvent>& /*events*/) { schedule(); }, error);
            if (id != 0) {
                watches.push_back(id);
            }
        }
    }
};

ScriptsWatcher::ScriptsWatcher(core::EventLoop& loop, core::WorkerPool& pool,
                               DirectoryWatcher& watcher, std::filesystem::path folder,
                               Handler handler)
    : impl_{std::make_unique<Impl>(loop, pool, watcher, std::move(folder), std::move(handler))} {
    if (!impl_->folder.empty()) {
        try {
            impl_->self = std::make_unique<core::FileWatcher>(
                loop, impl_->folder, [impl = impl_.get()] { impl->schedule(); }, kSettle);
        } catch (const std::exception& error) {
            // No folder to watch it in (a read-only data folder): read once, without following.
            log::warn("scripts: {} is not followed: {}", impl_->folder.string(), error.what());
        }
    }
    impl_->read();
}

ScriptsWatcher::~ScriptsWatcher() {
    *impl_->alive = false;
    impl_->reading.cancel();
    if (impl_->timer != 0) {
        impl_->loop.remove(impl_->timer);
    }
    for (const DirectoryWatcher::WatchId id : impl_->watches) {
        impl_->watcher.unwatch(id);
    }
}

}  // namespace nordend::fs
