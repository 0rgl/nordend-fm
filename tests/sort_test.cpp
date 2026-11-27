#include "nordend/fs/sort.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/fs/mime.hpp"

using nordend::fs::Entry;
using nordend::fs::EntryKind;
using nordend::fs::Listing;
using nordend::fs::natural_compare;
using nordend::fs::SortKey;
using nordend::fs::SortOrder;

namespace {

bool less(std::string_view a, std::string_view b) { return natural_compare(a, b) < 0; }

Entry file(std::string name, std::uint64_t size = 0, std::int64_t modified = 0) {
    return {
        .name = std::move(name), .kind = EntryKind::file, .size = size, .modified_ns = modified};
}

Entry dir(std::string name) { return {.name = std::move(name), .kind = EntryKind::directory}; }

std::vector<std::string> names(const std::vector<Entry>& entries) {
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (const Entry& entry : entries) {
        out.push_back(entry.name);
    }
    return out;
}

}  // namespace

TEST_CASE("natural order: numbers by value, case ignored") {
    CHECK(less("IMG_2.ORF", "IMG_10.ORF"));
    CHECK(less("IMG_9", "IMG_10"));
    CHECK(less("a", "B"));
    CHECK(less("B", "c"));
    CHECK(less("file", "file1"));
    CHECK(less("v1.9", "v1.10"));
    CHECK(less("2024-01-05", "2024-1-6"));  // 05 == 5, then 01 == 1: decided by 5 < 6
    CHECK(less("x99999999999999999999998", "x99999999999999999999999"));  // beyond 64 bits
    CHECK(natural_compare("same", "same") == 0);
}

TEST_CASE("natural order: ties are total and deterministic") {
    // Equal ignoring case and leading zeros: plain byte order decides.
    CHECK(less("File", "file"));
    CHECK(less("IMG_007", "IMG_7"));
    CHECK_FALSE(less("file", "File"));
    CHECK(natural_compare("a", "A") == -natural_compare("A", "a"));
}

TEST_CASE("natural order: in C, non-ASCII bytes after ASCII letters") {
    CHECK(less("zebra", "\xC3\xA8lan"));  // "èlan"
}

TEST_CASE("natural order: a dot before letters and digits") {
    CHECK(less("a.txt", "ab.txt"));
    CHECK(less("a.txt", "a1.txt"));
    CHECK(less("a.txt", "a0.txt"));
    CHECK(less(".hidden", "1"));
    CHECK(less("v1.9.txt", "v1.10.txt"));
    CHECK(less("v1.5", "v10"));
    // In C, space and hyphen come before the dot.
    CHECK(less("-a", ".hidden"));
    CHECK(less("photo (1).jpg", "photo.jpg"));
}

namespace {

// Sorts by a locale's collation while alive; false when it is not installed.
struct Collation {
    bool ok;

    explicit Collation(const char* locale) : ok{nordend::fs::set_collation(locale)} {}

    ~Collation() { nordend::fs::set_collation("C"); }

    Collation(const Collation&) = delete;
    Collation& operator=(const Collation&) = delete;
};

std::vector<std::string> sorted(std::vector<std::string> names) {
    std::ranges::sort(names, less);
    return names;
}

}  // namespace

TEST_CASE("natural order: the locale's collation") {
    SUBCASE("Polish") {
        const Collation polish{"pl_PL.UTF-8"};
        if (!polish.ok) {
            MESSAGE("pl_PL.UTF-8 not installed: skipped");
            return;
        }
        CHECK(sorted({"Zebra", "Łódź", "lody", "ćma", "dom", "cel", "Żaba", "zz"}) ==
              std::vector<std::string>{"cel", "ćma", "dom", "lody", "Łódź", "Zebra", "zz", "Żaba"});
        CHECK(less("ąb", "Ąc"));  // case is not the first rule
        CHECK(less("IMG_2.jpg", "IMG_10.jpg"));
        CHECK(less("a.txt", "ab.txt"));
        CHECK(less(".hidden", "a"));
        CHECK(less(".hidden", "-a"));
        CHECK(less("Report.doc", "report.pdf"));  // the whole name's letters before its case
        CHECK(natural_compare("ó", "ó") == 0);
        CHECK(less("File", "file") != less("file", "File"));
    }
    SUBCASE("German and French") {
        const Collation german{"de_DE.UTF-8"};
        if (!german.ok) {
            MESSAGE("de_DE.UTF-8 not installed: skipped");
            return;
        }
        CHECK(sorted({"ozon", "Österreich", "Ostern"}) ==
              std::vector<std::string>{"Ostern", "Österreich", "ozon"});
        const Collation french{"fr_FR.UTF-8"};
        if (!french.ok) {
            MESSAGE("fr_FR.UTF-8 not installed: skipped");
            return;
        }
        CHECK(sorted({"emu", "Émile", "zebra", "dame"}) ==
              std::vector<std::string>{"dame", "Émile", "emu", "zebra"});
    }
    SUBCASE("bytes that are not UTF-8 still sort, totally") {
        const Collation polish{"pl_PL.UTF-8"};
        if (!polish.ok) {
            MESSAGE("pl_PL.UTF-8 not installed: skipped");
            return;
        }
        CHECK(less("\xFF", "\xFF\xFE") != less("\xFF\xFE", "\xFF"));
        CHECK(natural_compare("a\xFFz", "a\xFFz") == 0);
    }
    SUBCASE("an unknown locale changes nothing") {
        CHECK_FALSE(nordend::fs::set_collation("xx_NOPE.UTF-8"));
        CHECK(less("zebra", "\xC3\xA8lan"));
    }
}

TEST_CASE("sort orders use the entries' keys") {
    const Collation polish{"pl_PL.UTF-8"};
    if (!polish.ok) {
        MESSAGE("pl_PL.UTF-8 not installed: skipped");
        return;
    }
    std::vector<Entry> entries{file("zz"), file("ćma"), file("dom")};
    entries[0].sort_key = nordend::fs::make_sort_key(entries[0].name);
    entries[1].sort_key = nordend::fs::make_sort_key(entries[1].name);  // one made when compared
    std::ranges::sort(entries, SortOrder{});
    CHECK(names(entries) == std::vector<std::string>{"ćma", "dom", "zz"});
}

TEST_CASE("sort orders") {
    std::vector<Entry> entries{file("b.txt", 300, 3), dir("Photos"), file("a10.txt", 100, 2),
                               file("a9.txt", 200, 1), dir("archive")};
    SortOrder order;
    std::ranges::sort(entries, order);
    CHECK(names(entries) ==
          std::vector<std::string>{"archive", "Photos", "a9.txt", "a10.txt", "b.txt"});

    order.descending = true;  // directories stay first
    std::ranges::sort(entries, order);
    CHECK(names(entries) ==
          std::vector<std::string>{"Photos", "archive", "b.txt", "a10.txt", "a9.txt"});

    order = {.key = SortKey::size};
    entries[0].size = 4096;  // a directory's own size does not order directories
    std::ranges::sort(entries, order);
    CHECK(names(entries) ==
          std::vector<std::string>{"archive", "Photos", "a10.txt", "a9.txt", "b.txt"});

    order = {.key = SortKey::modified, .directories_first = false};
    std::ranges::sort(entries, order);
    CHECK(names(entries) ==
          std::vector<std::string>{"archive", "Photos", "a9.txt", "a10.txt", "b.txt"});
}

TEST_CASE("sort by type: type names in order, unknown types last, then names") {
    const auto mime = nordend::fs::MimeDatabase::load();
    const auto typed = [&](const char* name, const char* type) {
        Entry entry = file(name, 1, 1);
        entry.mime = type == nullptr ? nordend::fs::kUnknownMime : mime->find(type);
        return entry;
    };
    std::vector<Entry> entries{typed("z.txt", "text/plain"),          typed("noext", nullptr),
                               typed("b.jpg", "image/jpeg"),          typed("a.txt", "text/plain"),
                               typed("c.orf", "image/x-olympus-orf"), dir("folder")};
    std::ranges::sort(entries, SortOrder{.key = SortKey::type, .mime = mime.get()});
    CHECK(names(entries) ==
          std::vector<std::string>{"folder", "b.jpg", "c.orf", "a.txt", "z.txt", "noext"});
}

TEST_CASE("Listing: chunks in any order give the fully sorted list") {
    std::vector<Entry> all;
    all.reserve(1001);
    for (int i = 0; i < 1000; ++i) {
        all.push_back(file("IMG_" + std::to_string(i) + ".ORF"));
    }
    all.push_back(dir("sub"));
    std::vector<Entry> expected = all;
    std::ranges::sort(expected, SortOrder{});

    std::mt19937 random{42};
    std::ranges::shuffle(all, random);
    Listing listing;
    for (std::size_t start = 0, size = 7; start < all.size(); start += size, size *= 2) {
        const std::size_t end = std::min(all.size(), start + size);
        listing.add({all.begin() + static_cast<std::ptrdiff_t>(start),
                     all.begin() + static_cast<std::ptrdiff_t>(end)});
    }
    CHECK(names(listing.entries()) == names(expected));
    CHECK(listing.entries().front().name == "sub");
    CHECK(listing.entries()[1].name == "IMG_0.ORF");
    CHECK(listing.entries().back().name == "IMG_999.ORF");

    listing.set_order({.descending = true});
    CHECK(listing.entries()[1].name == "IMG_999.ORF");
}

TEST_CASE("Listing: hidden entries left out and back, in order, kept up to date") {
    using Names = std::vector<std::string>;
    Listing listing;
    listing.add({file("b.txt"), file(".profile"), dir(".config"), file("a.txt")});
    CHECK(names(listing.entries()) == Names{".config", ".profile", "a.txt", "b.txt"});

    Listing::Moved moved = listing.set_filter(false, {});
    CHECK(names(listing.entries()) == Names{"a.txt", "b.txt"});
    CHECK(listing.hidden().size() == 2);
    std::ranges::sort(moved.hidden);
    CHECK(moved.hidden == Names{".config", ".profile"});
    CHECK(moved.shown.empty());

    // Arriving while left out; replaced and removed while left out.
    listing.add({file(".bashrc"), file("c.txt")});
    CHECK(names(listing.entries()) == Names{"a.txt", "b.txt", "c.txt"});
    listing.replace({".profile"}, {file(".bashrc", 10)});
    CHECK(listing.hidden().size() == 2);

    // One kept in sight: the way to a hidden folder opened further on.
    moved = listing.set_filter(false, ".config");
    CHECK(names(listing.entries()) == Names{".config", "a.txt", "b.txt", "c.txt"});
    CHECK(moved.shown == Names{".config"});

    moved = listing.set_filter(true, {});
    CHECK(names(listing.entries()) == Names{".config", ".bashrc", "a.txt", "b.txt", "c.txt"});
    CHECK(moved.shown == Names{".bashrc"});
    CHECK(listing.hidden().empty());
    CHECK(listing.entries()[1].size == 10);
}
