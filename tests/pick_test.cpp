// The file chooser's side in Nordend (ROADMAP 4b): filters (fs::FileFilterMatch), what a listing
// leaves out for them (Listing::set_only), and the Browser choosing files and folders.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <doctest/doctest.h>

#include "nordend/core/event_loop.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/directory_watcher.hpp"
#include "nordend/fs/entry.hpp"
#include "nordend/fs/file_filter.hpp"
#include "nordend/fs/mime.hpp"
#include "nordend/fs/sort.hpp"
#include "nordend/theme/palette.hpp"
#include "nordend/ui/geometry.hpp"
#include "nordend/ui/icons.hpp"
#include "nordend/ui/offscreen.hpp"
#include "nordend/ui/renderer.hpp"
#include "nordend/ui/seat.hpp"
#include "nordend/ui/text.hpp"
#include "nordend/views/browser.hpp"
#include "nordend/views/command_palette.hpp"
#include "nordend/views/folder.hpp"
#include "nordend/views/pick_bar.hpp"
#include "nordend/views/status_bar.hpp"
#include "temp_dir.hpp"

using nordend::core::EventLoop;
using nordend::core::WorkerPool;
using nordend::fs::DirectoryWatcher;
using nordend::fs::Entry;
using nordend::fs::EntryKind;
using nordend::fs::FileFilter;
using nordend::fs::FileFilterMatch;
using nordend::fs::MimeDatabase;
using nordend::test::TempDir;
using nordend::views::Browser;
using nordend::views::Folder;
using namespace std::chrono_literals;
namespace stdfs = std::filesystem;

namespace {

const std::shared_ptr<const MimeDatabase>& mime() {
    static const std::shared_ptr<const MimeDatabase> kDatabase = MimeDatabase::load();
    return kDatabase;
}

// An entry as the loader makes it: the type from its name when the name decides it.
Entry file(std::string name) {
    Entry entry;
    entry.name = std::move(name);
    entry.kind = EntryKind::file;
    const MimeDatabase::NameMatch match = mime()->by_name(entry.name);
    (match.certain ? entry.mime : entry.name_guess) = match.mime;
    return entry;
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

std::vector<std::string> names(const Folder& folder) {
    std::vector<std::string> out;
    for (const Entry& entry : folder.entries()) {
        out.push_back(entry.name);
    }
    return out;
}

// The save bar's name field as last drawn: where the bar shows a text cursor.
nordend::ui::Rect name_field(const nordend::views::PickBar& bar, float width, float height) {
    float left = width;
    float right = 0.0F;
    float top = height;
    float bottom = 0.0F;
    for (float y = 0.0F; y < height; y += 2.0F) {
        for (float x = 0.0F; x < width; x += 2.0F) {
            if (bar.editable_at(x, y)) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    return {left, top, right - left, bottom - top};
}

struct Fixture {
    TempDir dir;
    stdfs::path root = dir.path / "root";
    EventLoop loop;
    WorkerPool pool{loop, 2};
    DirectoryWatcher watcher{loop};
    Browser browser{loop, pool, watcher, mime(), [] {}, [](nordend::ui::CursorShape /*shape*/) {}};
    std::vector<std::vector<stdfs::path>> chosen;
    std::vector<std::optional<std::size_t>> filters;
    std::vector<std::vector<std::pair<std::string, std::string>>> choices;
    int cancelled = 0;

    Fixture() {
        stdfs::create_directories(root / "photos");
        for (const char* name : {"a.jpg", "B.JPG", "notes.txt", "main.cpp", "data-0001"}) {
            std::ofstream{root / name} << "x";
        }
    }

    void pick(Browser::Pick options) {
        browser.set_pick(
            std::move(options),
            [this](Browser::Picked picked) {
                chosen.push_back(std::move(picked.paths));
                filters.push_back(picked.filter);
                choices.push_back(std::move(picked.choices));
            },
            [this] { ++cancelled; });
        browser.columns().open(root);
        REQUIRE(run_until(loop, [this] { return !browser.loading(); }));
    }

    [[nodiscard]] std::shared_ptr<Folder> shown() { return browser.columns().focused_folder(); }

    void press(xkb_keysym_t key, nordend::ui::Modifiers modifiers = {}) {
        nordend::ui::KeyEvent event;
        event.keysym = key;
        event.modifiers = modifiers;
        browser.key(event, Browser::Clock::now());
    }

    // Typing into what has the keyboard, a character at a time.
    void type(std::string_view text) {
        for (const char c : text) {
            nordend::ui::KeyEvent event;
            event.keysym = static_cast<unsigned char>(c);
            event.text = std::string(1, c);
            browser.key(event, Browser::Clock::now());
        }
    }

    // The cursor onto name, alone.
    void cursor_on(const std::string& name) {
        shown()->select_name(name);
        REQUIRE(shown()->cursor_entry() != nullptr);
    }
};

const FileFilter kImages{.label = "Images", .globs = {"*.jpg"}, .types = {}};
const FileFilter kText{.label = "Text", .globs = {}, .types = {"text/plain"}};

}  // namespace

TEST_CASE("pick filter: globs without case, types with their subtypes, by the name alone") {
    const FileFilterMatch jpg{kImages, mime()};
    CHECK(jpg.passes(file("a.jpg")));
    CHECK(jpg.passes(file("B.JPG")));  // as in GTK 4
    CHECK_FALSE(jpg.passes(file("a.jpeg")));

    const FileFilterMatch text{kText, mime()};
    CHECK(text.passes(file("notes.txt")));
    CHECK(text.passes(file("main.cpp")));  // text/x-c++src is a text/plain
    CHECK_FALSE(text.passes(file("a.jpg")));
    CHECK(text.passes(file("README")));           // README* is text/x-readme, a text/plain
    CHECK_FALSE(text.passes(file("data-0001")));  // nothing in the name: octet-stream

    const FileFilterMatch images{{.label = "Images", .globs = {}, .types = {"image/*"}}, mime()};
    CHECK(images.passes(file("a.jpg")));
    CHECK(images.passes(file("P1010001.ORF")));  // image/x-olympus-orf
    CHECK_FALSE(images.passes(file("notes.txt")));

    const FileFilterMatch all{{.label = "All", .globs = {"*"}, .types = {}}, mime()};
    CHECK(all.passes(file("data-0001")));
    const FileFilterMatch every_type{{.label = "All", .globs = {}, .types = {"*/*"}}, mime()};
    CHECK(every_type.passes(file("data-0001")));
    // A type the database does not know matches nothing, without failing.
    const FileFilterMatch unknown{{.label = "?", .globs = {}, .types = {"x-no/such"}}, mime()};
    CHECK_FALSE(unknown.passes(file("a.jpg")));
}

TEST_CASE("listing: only what a filter passes, the rest kept aside and back when it changes") {
    nordend::fs::Listing listing;
    listing.add({file("a.jpg"), file("notes.txt"), file(".hidden.jpg")});
    auto moved = listing.set_only([](const Entry& entry) { return entry.name.ends_with(".jpg"); });
    CHECK(moved.hidden == std::vector<std::string>{"notes.txt"});
    REQUIRE(listing.entries().size() == 2);
    // Hidden files keep their own rule on top.
    listing.set_filter(false, {});
    REQUIRE(listing.entries().size() == 1);
    CHECK(listing.entries()[0].name == "a.jpg");
    // A new entry follows the filter as it arrives.
    listing.add({file("b.txt"), file("c.jpg")});
    CHECK(listing.entries().size() == 2);
    moved = listing.set_only({});
    std::ranges::sort(moved.shown);  // kept aside in no order
    CHECK(moved.shown == std::vector<std::string>{"b.txt", "notes.txt"});
    CHECK(listing.entries().size() == 4);
}

TEST_CASE("pick: the filter decides the files listed; folders always show") {
    Fixture f;
    Browser::Pick options;
    options.accept_label = "_Open";
    options.filters = {kImages, kText};
    f.pick(options);
    CHECK(f.browser.pick_bar().accept_label() == "Open");
    REQUIRE(f.browser.pick_bar().items().size() == 1);
    CHECK(f.browser.pick_bar().items()[0].text == "Images");
    CHECK(names(*f.shown()) == std::vector<std::string>{"photos", "a.jpg", "B.JPG"});
    CHECK(nordend::views::status_of(*f.shown()) == "3 items, 3 filtered out");

    f.browser.set_pick_filter(1);
    CHECK(f.browser.pick_bar().items()[0].text == "Text");
    CHECK(names(*f.shown()) == std::vector<std::string>{"photos", "main.cpp", "notes.txt"});

    // Enter on a file chooses it, with the filter in use.
    f.cursor_on("notes.txt");
    f.press(XKB_KEY_Return);
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0] == std::vector<stdfs::path>{f.root / "notes.txt"});
    CHECK(f.filters[0] == std::optional<std::size_t>{1});

    // Two selected without --multiple: a word in the status bar, nothing chosen.
    f.press(XKB_KEY_Up, {.shift = true});
    f.press(XKB_KEY_Return);
    CHECK(f.chosen.size() == 1);
    CHECK(f.browser.status_bar().message() == "Choose one file");

    // Ctrl+Enter on a folder goes into it, as Enter does.
    f.cursor_on("photos");
    f.press(XKB_KEY_Return, {.ctrl = true});
    REQUIRE(run_until(f.loop, [&f] { return !f.browser.loading(); }));
    CHECK(f.chosen.size() == 1);
    CHECK(f.shown()->path() == f.root / "photos");

    f.press(XKB_KEY_Escape);
    CHECK(f.cancelled == 1);
}

TEST_CASE("pick: several files with --multiple; no filter, every file and no filter line") {
    Fixture f;
    Browser::Pick options;
    options.multiple = true;
    f.pick(options);
    CHECK(f.browser.pick_bar().items().empty());
    CHECK(names(*f.shown()).size() == 6);
    f.cursor_on("a.jpg");
    f.press(XKB_KEY_Down, {.shift = true});  // a.jpg, B.JPG
    f.browser.accept_pick();
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0] == std::vector<stdfs::path>{f.root / "a.jpg", f.root / "B.JPG"});
    CHECK_FALSE(f.filters[0].has_value());
}

TEST_CASE("pick: a selection is chosen even when the cursor's file went away") {
    Fixture f;
    Browser::Pick options;
    options.multiple = true;
    f.pick(options);
    f.cursor_on("a.jpg");
    f.press(XKB_KEY_Down, {.shift = true});
    f.press(XKB_KEY_Down, {.shift = true});  // a.jpg, B.JPG, data-0001
    REQUIRE(f.shown()->selected_count() == 3);
    const std::string under_cursor = f.shown()->cursor_entry()->name;
    stdfs::remove(f.root / under_cursor);  // an application tidying the folder meanwhile
    REQUIRE(run_until(f.loop, [&f] { return f.shown()->entries().size() == 5; }));
    REQUIRE(f.shown()->cursor_entry() == nullptr);  // the cursor lost its file, the rest stays
    REQUIRE(f.shown()->selected_count() == 2);

    f.browser.accept_pick();
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0].size() == 2);
    f.press(XKB_KEY_Return);  // Enter does the same
    REQUIRE(f.chosen.size() == 2);
    CHECK(f.chosen[1] == f.chosen[0]);
}

TEST_CASE("pick: folders, the one under the cursor or else the one shown") {
    Fixture f;
    Browser::Pick options;
    options.folders = true;
    options.filters = {kImages};
    f.pick(options);
    CHECK(f.browser.pick_bar().accept_label() == "Select");
    CHECK(f.browser.pick_bar().items().empty());  // filters are for files
    CHECK(names(*f.shown()) == std::vector<std::string>{"photos"});

    f.cursor_on("photos");
    f.press(XKB_KEY_Return, {.ctrl = true});
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0] == std::vector<stdfs::path>{f.root / "photos"});

    // Enter opens it as always; inside, nothing under the cursor: the folder shown.
    f.press(XKB_KEY_Return);
    REQUIRE(run_until(f.loop, [&f] { return !f.browser.loading(); }));
    REQUIRE(f.shown()->path() == f.root / "photos");
    CHECK(f.shown()->cursor_entry() == nullptr);
    f.browser.accept_pick();
    REQUIRE(f.chosen.size() == 2);
    CHECK(f.chosen[1] == std::vector<stdfs::path>{f.root / "photos"});
}

TEST_CASE("pick bar: the application's word without its mnemonic") {
    CHECK(nordend::views::without_mnemonic("_Open") == "Open");
    CHECK(nordend::views::without_mnemonic("Save __as") == "Save _as");
    CHECK(nordend::views::without_mnemonic("") == "");
}

TEST_CASE("pick: saving, the name typed into the folder shown, asked before replacing") {
    Fixture f;
    Browser::Pick options;
    options.save = true;
    options.name = "holiday.jpg";
    f.pick(options);
    CHECK(f.browser.pick_bar().accept_label() == "Save");
    CHECK(f.browser.pick_bar().saving());
    CHECK(f.browser.pick_bar().name_focused());
    CHECK(f.browser.pick_bar().name() == "holiday.jpg");

    // The stem is selected: typing replaces it, the extension stays.
    f.type("sea");
    CHECK(f.browser.pick_bar().name() == "sea.jpg");
    f.press(XKB_KEY_Return);
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0] == std::vector<stdfs::path>{f.root / "sea.jpg"});

    // A name already there: a card first; Escape there keeps the chooser open.
    f.press(XKB_KEY_a, {.ctrl = true});
    f.type("a.jpg");
    f.press(XKB_KEY_Return);
    CHECK(f.chosen.size() == 1);
    REQUIRE(f.browser.confirm_dialog().is_open());
    CHECK(f.browser.confirm_dialog().title() == "Replace \u201ca.jpg\u201d?");
    f.press(XKB_KEY_Escape);
    CHECK_FALSE(f.browser.confirm_dialog().is_open());
    CHECK(f.cancelled == 0);
    f.press(XKB_KEY_Return);
    REQUIRE(f.browser.confirm_dialog().is_open());
    f.press(XKB_KEY_Return);  // Replace
    REQUIRE(f.chosen.size() == 2);
    CHECK(f.chosen[1] == std::vector<stdfs::path>{f.root / "a.jpg"});

    // A folder's name: into it.
    f.press(XKB_KEY_a, {.ctrl = true});
    f.type("photos");
    f.press(XKB_KEY_Return);
    REQUIRE(run_until(f.loop, [&f] { return !f.browser.loading(); }));
    CHECK(f.shown()->path() == f.root / "photos");
    CHECK(f.chosen.size() == 2);

    // Names that cannot be.
    f.press(XKB_KEY_a, {.ctrl = true});
    f.press(XKB_KEY_BackSpace);
    f.press(XKB_KEY_Return);
    CHECK(f.browser.status_bar().message() == "Type a name for the file");
    f.type("a/b");
    f.press(XKB_KEY_Return);
    CHECK(f.chosen.size() == 2);
}

TEST_CASE(
    "pick: saving, Tab between the name and the view; a file the cursor goes to lends its name") {
    Fixture f;
    Browser::Pick options;
    options.save = true;
    options.name = "new.txt";
    f.pick(options);
    f.press(XKB_KEY_Tab);
    CHECK_FALSE(f.browser.pick_bar().name_focused());
    // Opening the folder did not take the name away; a move inside it does.
    f.browser.follow_pick_cursor();
    CHECK(f.browser.pick_bar().name() == "new.txt");
    f.cursor_on("notes.txt");
    f.browser.follow_pick_cursor();
    CHECK(f.browser.pick_bar().name() == "notes.txt");
    f.press(XKB_KEY_Tab);
    CHECK(f.browser.pick_bar().name_focused());
}

TEST_CASE("pick: an application's choices, listed and checked, come back with the choice") {
    Fixture f;
    Browser::Pick options;
    options.choices = {{.id = "encoding",
                        .label = "Encoding",
                        .options = {{"utf8", "UTF-8"}, {"latin1", "Latin-1"}},
                        .selected = "latin1"},
                       {.id = "ro", .label = "Read-only", .options = {}, .selected = "maybe"}};
    f.pick(options);
    const std::vector<nordend::views::PickBar::Item>& items = f.browser.pick_bar().items();
    REQUIRE(items.size() == 2);
    CHECK(items[0].text == "Encoding: Latin-1");
    CHECK(items[1].checkbox);
    CHECK_FALSE(items[1].checked);  // an unknown default: unchecked
    f.browser.set_pick_choice(1, "true");
    CHECK(f.browser.pick_bar().items()[1].checked);
    f.browser.set_pick_choice(0, "utf8");
    CHECK(f.browser.pick_bar().items()[0].text == "Encoding: UTF-8");
    f.cursor_on("notes.txt");
    f.press(XKB_KEY_Return);
    REQUIRE(f.choices.size() == 1);
    CHECK(f.choices[0] ==
          std::vector<std::pair<std::string, std::string>>{{"encoding", "utf8"}, {"ro", "true"}});
}

TEST_CASE("pick: saving several files, into the folder chosen, asked once for those there") {
    Fixture f;
    Browser::Pick options;
    options.files = {"a.jpg", "new.txt", "notes.txt"};
    f.pick(options);
    CHECK(f.browser.pick_bar().accept_label() == "Save");
    CHECK_FALSE(f.browser.pick_bar().saving());  // no name field: the names are given
    CHECK(names(*f.shown()) == std::vector<std::string>{"photos"});
    f.browser.accept_pick();  // nothing under the cursor: the folder shown
    REQUIRE(f.browser.confirm_dialog().is_open());
    CHECK(f.browser.confirm_dialog().title() == "Replace 2 files?");
    f.press(XKB_KEY_Return);
    REQUIRE(f.chosen.size() == 1);
    CHECK(f.chosen[0] ==
          std::vector<stdfs::path>{f.root / "a.jpg", f.root / "new.txt", f.root / "notes.txt"});
    // Into a folder without any of them: no question.
    f.cursor_on("photos");
    f.browser.accept_pick();
    CHECK_FALSE(f.browser.confirm_dialog().is_open());
    REQUIRE(f.chosen.size() == 2);
    CHECK(f.chosen[1].front() == f.root / "photos" / "a.jpg");
}

TEST_CASE(
    "pick: saving, the name's field selects with the pointer: a drag, Shift+click, double click") {
    constexpr std::int32_t width = 1000;
    constexpr std::int32_t height = 700;
    std::unique_ptr<nordend::ui::Offscreen> gl;
    try {
        gl = std::make_unique<nordend::ui::Offscreen>(width, height);
    } catch (const std::runtime_error& error) {
        MESSAGE("no offscreen OpenGL: " << error.what());
        return;
    }
    nordend::ui::FontSystem fonts;
    nordend::ui::Renderer renderer{fonts};
    nordend::ui::IconSet icons{fonts};
    Fixture f;
    Browser::Pick options;
    options.save = true;
    options.name = "holiday.jpg";
    f.pick(options);
    const auto draw = [&] {
        renderer.begin_frame(width, height, 1.0F, {});
        f.browser.draw(renderer, {0.0F, 0.0F, width, height}, nordend::theme::Palette{}, icons,
                       Browser::Clock::now());
        renderer.end_frame();
    };
    draw();
    REQUIRE(f.browser.pick_bar().name() == "holiday.jpg");
    const nordend::ui::Rect box = name_field(f.browser.pick_bar(), width, height);
    REQUIRE(box.width > 0.0F);
    const float left = box.x;
    const float right = box.right();
    const float y = box.y + (box.height / 2.0F);
    using Kind = nordend::ui::PointerEvent::Kind;
    const auto pointer = [&](Kind kind, float x, float at_y, int clicks = 1, bool shift = false) {
        f.browser.pointer({.kind = kind,
                           .x = x,
                           .y = at_y,
                           .button = nordend::ui::PointerButton::left,
                           .click_count = clicks,
                           .modifiers = {.shift = shift}},
                          Browser::Clock::now());
    };

    // A drag from the start of the name past the bar's edge: all of it.
    pointer(Kind::press, left + 1.0F, y);
    pointer(Kind::motion, width - 1.0F, 5.0F);
    pointer(Kind::release, width - 1.0F, 5.0F);
    f.type("x");
    CHECK(f.browser.pick_bar().name() == "x");
    f.press(XKB_KEY_z, {.ctrl = true});
    REQUIRE(f.browser.pick_bar().name() == "holiday.jpg");

    // A click at the start, Shift+click at the end.
    pointer(Kind::press, left + 1.0F, y);
    pointer(Kind::release, left + 1.0F, y);
    pointer(Kind::press, right - 1.0F, y, 1, true);
    pointer(Kind::release, right - 1.0F, y);
    f.type("y");
    CHECK(f.browser.pick_bar().name() == "y");
    f.press(XKB_KEY_z, {.ctrl = true});
    REQUIRE(f.browser.pick_bar().name() == "holiday.jpg");

    // A double click: all of it.
    pointer(Kind::press, (left + right) / 2.0F, y);
    pointer(Kind::release, (left + right) / 2.0F, y);
    pointer(Kind::press, (left + right) / 2.0F, y, 2);
    pointer(Kind::release, (left + right) / 2.0F, y);
    f.type("z");
    CHECK(f.browser.pick_bar().name() == "z");
}

TEST_CASE("pick: saving, a drag in the name ends when an overlay takes the button's release") {
    constexpr std::int32_t width = 1000;
    constexpr std::int32_t height = 700;
    std::unique_ptr<nordend::ui::Offscreen> gl;
    try {
        gl = std::make_unique<nordend::ui::Offscreen>(width, height);
    } catch (const std::runtime_error& error) {
        MESSAGE("no offscreen OpenGL: " << error.what());
        return;
    }
    nordend::ui::FontSystem fonts;
    nordend::ui::Renderer renderer{fonts};
    nordend::ui::IconSet icons{fonts};
    // F1 opens the shortcuts over the bar, Ctrl+K the palette.
    for (const bool palette : {false, true}) {
        CAPTURE(palette);
        Fixture f;
        Browser::Pick options;
        options.save = true;
        options.name = "holiday.jpg";
        f.pick(options);
        const auto draw = [&] {
            renderer.begin_frame(width, height, 1.0F, {});
            f.browser.draw(renderer, {0.0F, 0.0F, width, height}, nordend::theme::Palette{}, icons,
                           Browser::Clock::now());
            renderer.end_frame();
        };
        draw();
        const nordend::ui::Rect box = name_field(f.browser.pick_bar(), width, height);
        REQUIRE(box.width > 0.0F);
        const float y = box.y + (box.height / 2.0F);
        using Kind = nordend::ui::PointerEvent::Kind;
        const auto pointer = [&](Kind kind, float x) {
            f.browser.pointer({.kind = kind,
                               .x = x,
                               .y = y,
                               .button = nordend::ui::PointerButton::left,
                               .click_count = 1,
                               .modifiers = {}},
                              Browser::Clock::now());
        };

        pointer(Kind::press, box.right() - 1.0F);  // the caret at the end, a drag starts
        if (palette) {
            f.press(XKB_KEY_k, {.ctrl = true});
        } else {
            f.press(XKB_KEY_F1);
        }
        draw();
        pointer(Kind::release, box.right() - 1.0F);  // the overlay's
        f.press(XKB_KEY_Escape);
        draw();

        // The button is up: moving selects nothing, a click places the caret.
        pointer(Kind::motion, box.x + 1.0F);
        f.type("q");
        CHECK(f.browser.pick_bar().name() == "holiday.jpgq");
        f.press(XKB_KEY_z, {.ctrl = true});
        REQUIRE(f.browser.pick_bar().name() == "holiday.jpg");
        pointer(Kind::press, box.x + 1.0F);
        pointer(Kind::release, box.x + 1.0F);
        f.type("q");
        CHECK(f.browser.pick_bar().name() == "qholiday.jpg");
    }
}

TEST_CASE("pick: selecting by name works when opening, not while saving") {
    Fixture f;
    Browser::Pick options;
    options.multiple = true;
    f.pick(options);
    f.press(XKB_KEY_s, {.ctrl = true});
    REQUIRE(f.browser.command_palette().is_open());
    f.type("*.jpg");
    f.press(XKB_KEY_Return);
    CHECK(f.shown()->selected_names() == std::vector<std::string>{"a.jpg", "B.JPG"});

    Fixture g;
    Browser::Pick saving;
    saving.save = true;
    saving.name = "new.txt";
    g.pick(saving);
    g.press(XKB_KEY_Tab);  // the keyboard to the view
    g.press(XKB_KEY_s, {.ctrl = true});
    CHECK_FALSE(g.browser.command_palette().is_open());
    CHECK(g.browser.status_bar().message() == "Nothing is selected by name while saving");
    CHECK(g.browser.pick_bar().name() == "new.txt");
    const std::vector<nordend::views::PaletteItem> items = g.browser.palette_items();
    CHECK(std::ranges::none_of(items, [](const nordend::views::PaletteItem& item) {
        return item.id == "command:select_pattern";
    }));
}
