#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "nordend/archive/archive.hpp"
#include "nordend/engines/engines.hpp"
#include "nordend/picture/load.hpp"
#include "nordend/picture/photo_info.hpp"
#include "nordend/text/text.hpp"

// Nordend's preview helpers (ROADMAP 2b): the parsers of files that come from outside run in
// separate nordend-preview processes, started when a picture is first asked for, at most one
// per worker, each closed (and waited for, no zombie) after a while without work. A helper that
// stops, or takes too long, is killed: the picture is "unavailable" and Nordend carries on. Nothing
// is ever parsed here instead.
namespace nordend::isolate {

namespace protocol {
struct Request;
struct Packet;
}  // namespace protocol

struct HelperOptions {
    std::filesystem::path program;            // nordend-preview
    std::vector<std::string> args;            // extra arguments (tests)
    std::size_t max_helpers{8};               // one for each worker: no picture waits for a helper
    std::chrono::milliseconds timeout{3000};  // for one picture
    std::chrono::milliseconds idle{30'000};   // a helper without work is closed after this
};

class Helpers {
public:
    explicit Helpers(HelperOptions options);
    ~Helpers();
    Helpers(const Helpers&) = delete;
    Helpers& operator=(const Helpers&) = delete;
    Helpers(Helpers&&) = delete;
    Helpers& operator=(Helpers&&) = delete;

    // The picture of request.path, made by a helper. Blocks while every helper is busy.
    // Thread-safe: called from the workers.
    [[nodiscard]] picture::Loaded load(const picture::LoadRequest& request);

    // What path says about itself (EXIF), read by a helper;
    // empty when it cannot be read. Thread-safe.
    [[nodiscard]] picture::PhotoInfo photo_info(const std::filesystem::path& path);

    // The text of request.path, read by a helper. Thread-safe.
    [[nodiscard]] text::Loaded load_text(const text::LoadRequest& request);

    // An archive's entries, and one entry's bytes written to out, by a helper; the time allowed
    // grows with the archive (a tar.gz is inflated from its start). Thread-safe.
    [[nodiscard]] archive::Listing list_archive(const std::filesystem::path& path,
                                                const archive::Limits& limits);
    [[nodiscard]] archive::Read read_archive_entry(const std::filesystem::path& path,
                                                   archive::Format format,
                                                   const archive::Entry& entry, int out,
                                                   std::uint64_t max_bytes);

    // Reads of path's entries in a row, all by one helper, taken until the sequence goes and
    // ended with it (a tar.gz is inflated once when the entries come in its order). helpers
    // stays alive as long.
    // stopped, when set, is asked every kStopCheck while a read waits: true stops the helper.
    [[nodiscard]] static std::unique_ptr<archive::EntrySequence> open_sequence(
        std::shared_ptr<Helpers> helpers, const std::filesystem::path& path, archive::Format format,
        std::function<bool()> stopped = {});

    static constexpr std::chrono::milliseconds kStopCheck{100};

    // Helpers started and not yet known to be gone (tests).
    [[nodiscard]] std::size_t running() const;

private:
    struct Helper {
        pid_t pid{-1};
        int socket{-1};
        std::chrono::steady_clock::time_point idle_since;
    };
    enum class Outcome : std::uint8_t { done, again, failed };
    class Sequence;

    [[nodiscard]] std::optional<Helper> take();
    void give_back(Helper helper);
    int discard(Helper helper);  // its wait status
    void close_idle();           // the reaper thread: closes and waits for helpers idle too long
    [[nodiscard]] std::optional<Helper> spawn() const;
    // One request and its reply; again: the helper had left (retry in a new one).
    static Outcome exchange(Helper helper, std::span<const std::byte> request,
                            std::span<const int> fds, protocol::Packet& reply, std::string& why,
                            std::chrono::milliseconds timeout,
                            const std::function<bool()>& stopped = {});
    // The request to a helper (a new one if it had left), its reply handed to accept; false with
    // why when no helper answered in a way accept took.
    bool run(std::span<const std::byte> request, std::span<const int> fds,
             const std::function<bool(protocol::Packet&, std::string&)>& accept, std::string& why,
             std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    HelperOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable freed_;
    std::vector<Helper> idle_;
    std::size_t count_{0};  // idle and busy
    std::condition_variable idle_changed_;
    bool stopping_{false};
    std::thread reaper_;  // declared last: started once the rest is ready
};

// Sends pictures, photo information and texts to the engines of registry that make them, each
// engine through its own helpers (options.program is set from the engine): pictures to the first
// engine with a family showing pictures (load_picture names no type to choose by), the
// information to it when a family of it has info, texts to the first engine with a family
// showing text, archives to the first with a family showing archives. A warning in the log for each
// that no engine makes. The helpers live as long as the loaders set.
void use_engines(const engines::Registry& registry, HelperOptions options = {});

}  // namespace nordend::isolate
