#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nordend/core/event_loop.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/sort.hpp"
#include "nordend/views/grid_size.hpp"

// What Nordend remembers from one start to the next (ROADMAP 7.5a): how the window was left, not
// preferences (those are config.toml's). Kept in $XDG_STATE_HOME/nordend/state.toml, written by
// Nordend; a person need not edit it, and a broken one only means the defaults.
namespace nordend::views {

// A new field goes into merged_state() too.
struct State {
    enum class Mode : std::uint8_t { columns, grid, details };

    Mode mode{Mode::columns};
    bool show_hidden{false};
    bool sidebar_visible{true};
    float sidebar_width{200.0F};
    bool info_visible{true};
    float info_width{300.0F};
    bool info_expanded{false};
    // The details' order and the widths of size, modified and type (the name takes the rest).
    fs::SortKey sort_key{fs::SortKey::name};
    bool sort_descending{false};
    std::array<float, 3> details_widths{96.0F, 150.0F, 220.0F};
    // The lists' row size (an index into kListSizes, 1 the usual) and the grid's icon size in
    // pixels (one of kGridIconSizes).
    std::size_t list_size{1};
    std::int32_t grid_icon{kGridIconSizes[kUsualGridSize]};
    // The window's size when it was last neither maximized, fullscreen nor tiled; logical pixels.
    std::int32_t window_width{960};
    std::int32_t window_height{640};
    bool maximized{false};

    // The tabs when there were two or more (ROADMAP 9.1d), restored when Nordend starts without
    // a folder; empty with one tab. Each has its folder and view; in the columns, the first
    // column's folder too, so they unfold again from there.
    struct Tab {
        std::filesystem::path folder;
        Mode mode{Mode::columns};
        std::filesystem::path root;  // the columns' first folder; empty in the other views
        std::string cursor;          // the name under the cursor in folder; empty: none
        // The tab's zoom; none (a tab written before): the window's list_size and grid_icon.
        std::optional<std::size_t> list_size;
        std::optional<std::int32_t> grid_icon;

        bool operator==(const Tab&) const = default;
    };

    static constexpr std::size_t kMaxTabs = 64;

    std::vector<Tab> tabs;
    std::size_t shown_tab{0};  // which of tabs was shown

    // The command palette's recently used items (their ids), most recent first (ROADMAP 8a).
    static constexpr std::size_t kMaxRecent = 10;
    std::vector<std::string> palette_recent;

    bool operator==(const State&) const = default;
};

struct StateResult {
    State state;
    // One line per problem, ready to log: "<file>:<line>:<column>: <what>".
    std::vector<std::string> problems;
    // What is wrong with the file as a whole; state is then the defaults.
    bool missing{false};     // not there (not a problem)
    bool unreadable{false};  // there, but could not be read
    bool broken{false};      // read, but not TOML
};

// state.toml under $XDG_STATE_HOME (or ~/.local/state); empty without a home.
[[nodiscard]] std::filesystem::path default_state_path();

// Reads state.toml text. A value of the wrong type or out of range keeps its default and is a
// problem; unknown keys are ignored (a newer Nordend may have written them). A file that does not
// parse gives the defaults.
[[nodiscard]] StateResult parse_state(std::string_view text, std::string_view source);

// A missing file is not a problem: the defaults.
[[nodiscard]] StateResult load_state(const std::filesystem::path& path);

[[nodiscard]] std::string format_state(const State& state);

// What another window wrote (theirs), with what this one changed since base (mine) put over it.
// Fields that go together change together: the tabs with the one shown, the window's size with
// maximized, the order with its direction.
[[nodiscard]] State merged_state(const State& base, const State& mine, const State& theirs);

// Keeps state.toml current: a change is written a moment after the last one (a dragged edge
// writes once), on a worker, one write at a time so an older state never lands after a newer
// one. flush() writes what is left, on the calling thread (at exit); a worker's write still
// running then cannot land after it. Each write reads the file first and changes only what this
// window changed (merged_state), under a lock on the folder: another window's changes stay.
//
// Loop thread only. Must not outlive the loop or the pool.
class StateKeeper {
public:
    static constexpr std::chrono::milliseconds kDelay{1000};
    static constexpr std::chrono::milliseconds kRetry{10000};  // after a write that failed

    // saved: what the file holds now, as loaded at start.
    StateKeeper(core::EventLoop& loop, core::WorkerPool& pool, std::filesystem::path path,
                State saved, std::chrono::milliseconds delay = kDelay);
    ~StateKeeper();

    StateKeeper(const StateKeeper&) = delete;
    StateKeeper& operator=(const StateKeeper&) = delete;
    StateKeeper(StateKeeper&&) = delete;
    StateKeeper& operator=(StateKeeper&&) = delete;

    // The state now; cheap when nothing changed (main calls it after every frame).
    void note(const State& state);

    void flush();

    // The saved tabs were not restored (a window opened on a folder): its single tab does not
    // count as a change, so another window's tabs stay.
    void opened_without_tabs();

    // What was written last (tests).
    [[nodiscard]] const State& saved() const noexcept { return saved_; }

private:
    struct File;

    void write();

    core::EventLoop& loop_;
    core::WorkerPool& pool_;
    std::filesystem::path path_;
    std::chrono::milliseconds delay_;
    State saved_;    // in the file, or being written there
    State current_;  // the latest noted
    core::EventLoop::SourceId timer_{0};
    bool writing_{false};
    bool retry_{false};            // the last write failed: write again even with nothing new
    std::string told_;             // the last problem logged, said once
    std::uint64_t generation_{0};  // of current_
    std::shared_ptr<File> file_;   // shared with the worker's write
};

}  // namespace nordend::views
