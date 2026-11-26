#include "nordend/core/file_watcher.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/inotify.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "nordend/core/log.hpp"

namespace nordend::core {
namespace {

// Writes closed, renames into and out of the directory, deletions. IN_MODIFY is left out on
// purpose: it fires on every write() and after a truncation, before the content is complete.
constexpr std::uint32_t kEvents =
    IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_CREATE;

}  // namespace

// One inotify instance for a loop's watchers, and each directory's watch shared by the watchers of
// files in it: inotify_add_watch gives the same descriptor for a directory already watched, so
// each is counted, and removed with its last watcher. Used on the loop thread only; the registry
// of instances is shared by every loop, hence its mutex.
//
// One queue for all the directories watched: when it overflows, every watcher of the loop hears of
// it and reloads, not only those of the busy directory. Their handlers only read a file again.
class FileWatcher::Shared {
public:
    explicit Shared(EventLoop& loop) : loop_{loop}, fd_{inotify_init1(IN_NONBLOCK | IN_CLOEXEC)} {
        if (fd_ < 0) {
            throw std::system_error{errno, std::generic_category(), "inotify_init1"};
        }
        try {
            source_ = loop_.add_fd(fd_, POLLIN, [this](short /*revents*/) { on_readable(); });
        } catch (...) {
            close(fd_);
            throw;
        }
    }

    ~Shared() {
        loop_.remove(source_);
        close(fd_);
        const std::scoped_lock lock{registry_mutex()};
        registry().erase(&loop_);
    }

    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
    Shared(Shared&&) = delete;
    Shared& operator=(Shared&&) = delete;

    // The one for loop, made when it has none (the registry only holds it weakly: it goes with
    // the loop's last watcher).
    static std::shared_ptr<Shared> of(EventLoop& loop) {
        {
            const std::scoped_lock lock{registry_mutex()};
            const auto it = registry().find(&loop);
            if (it != registry().end()) {
                if (std::shared_ptr<Shared> found = it->second.lock()) {
                    return found;
                }
            }
        }
        // Made outside the lock: a failed one must not leave an entry, and ~Shared takes the
        // lock. Not make_shared, whose single block the registry's weak_ptr would keep.
        std::shared_ptr<Shared> made{new Shared{loop}};
        const std::scoped_lock lock{registry_mutex()};
        registry()[&loop] = made;
        return made;
    }

    // Watches folder for watcher; its watch descriptor.
    int add(const std::filesystem::path& folder, FileWatcher* watcher) {
        // What is queued already belongs to the watchers there before: read and handed out now,
        // so that a watcher joining a directory's watch does not hear of older changes.
        on_readable();
        const int watch = inotify_add_watch(fd_, folder.c_str(), kEvents);
        if (watch < 0) {
            throw std::system_error{errno, std::generic_category(),
                                    "inotify_add_watch " + folder.string()};
        }
        watches_[watch].push_back(watcher);
        return watch;
    }

    void remove(int watch, FileWatcher* watcher) {
        const auto it = watches_.find(watch);
        if (it == watches_.end()) {
            return;  // dropped by the kernel already (its directory went)
        }
        std::erase(it->second, watcher);
        if (it->second.empty()) {
            inotify_rm_watch(fd_, watch);
            watches_.erase(it);
        }
    }

private:
    static std::mutex& registry_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    static std::map<EventLoop*, std::weak_ptr<Shared>>& registry() {
        static std::map<EventLoop*, std::weak_ptr<Shared>> instances;
        return instances;
    }

    void on_readable() {
        // Big enough for many events with names; aligned as inotify requires.
        alignas(inotify_event) std::array<char, 4096> buffer{};
        std::vector<FileWatcher*> touched;
        bool overflow = false;
        for (;;) {
            const ssize_t length = read(fd_, buffer.data(), buffer.size());
            if (length <= 0) {
                if (length < 0 && errno != EAGAIN && errno != EINTR) {
                    log::warn("file watcher: read failed: {}", std::strerror(errno));  // NOLINT
                }
                break;
            }
            for (std::size_t offset = 0; std::cmp_less(offset, length);) {
                inotify_event event{};
                std::memcpy(&event, buffer.data() + offset, sizeof event);
                const char* name = buffer.data() + offset + sizeof event;
                offset += sizeof event + event.len;
                // On a queue overflow events were lost: assume every file changed.
                if ((event.mask & IN_Q_OVERFLOW) != 0U) {
                    overflow = true;
                    continue;
                }
                const auto it = watches_.find(event.wd);
                if (it == watches_.end()) {
                    continue;
                }
                if ((event.mask & IN_IGNORED) != 0U) {
                    // The kernel may give the number to another directory later: let go of it.
                    for (FileWatcher* watcher : it->second) {
                        log::warn("file watcher: directory of {} gone, changes no longer followed",
                                  watcher->name_);
                        watcher->watch_ = -1;
                    }
                    watches_.erase(it);
                    continue;
                }
                if (event.len == 0) {
                    continue;
                }
                for (FileWatcher* watcher : it->second) {
                    if (watcher->name_ == name) {
                        touched.push_back(watcher);
                    }
                }
            }
        }
        if (overflow) {
            touched.clear();
            for (const auto& [watch, watchers] : watches_) {
                touched.insert(touched.end(), watchers.begin(), watchers.end());
            }
        }
        // changed() only starts a timer: no watcher is added or removed meanwhile.
        for (FileWatcher* watcher : touched) {
            watcher->changed();
        }
    }

    EventLoop& loop_;
    int fd_;
    EventLoop::SourceId source_{0};
    std::map<int, std::vector<FileWatcher*>> watches_;  // watch descriptor: its watchers
};

FileWatcher::FileWatcher(EventLoop& loop, const std::filesystem::path& file, Handler handler,
                         std::chrono::milliseconds settle, Directory directory)
    : loop_{loop},
      name_{file.filename().string()},
      handler_{std::move(handler)},
      settle_{settle},
      shared_{Shared::of(loop)} {
    const std::filesystem::path folder = file.parent_path();
    if (directory == Directory::create) {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
    }
    watch_ = shared_->add(folder, this);
}

FileWatcher::~FileWatcher() {
    loop_.remove(timer_);
    shared_->remove(watch_, this);
}

void FileWatcher::changed() {
    if (timer_ != 0) {
        return;  // a pending timer already covers this change
    }
    timer_ = loop_.add_timer(settle_, std::chrono::milliseconds{0}, [this] {
        timer_ = 0;
        handler_();
    });
}

}  // namespace nordend::core
