#include "nordend/fs/sort.hpp"

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nordend::fs {
namespace {

// The collation, or null for the "C" order (bytes, ASCII case folded).
std::atomic<locale_t> g_collation{nullptr};

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

char fold(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

int sign(int value) noexcept { return static_cast<int>(value > 0) - static_cast<int>(value < 0); }

template <class T>
int three_way(T a, T b) noexcept {
    return static_cast<int>(a > b) - static_cast<int>(a < b);
}

// text through strxfrm_l; text itself if strxfrm_l fails.
std::string transformed(const std::string& text, locale_t locale) {
    std::string key((text.size() * 8) + 16, '\0');
    std::size_t made = strxfrm_l(key.data(), text.c_str(), key.size(), locale);
    if (made == static_cast<std::size_t>(-1)) {
        return text;
    }
    if (made >= key.size()) {
        key.resize(made + 1);
        made = strxfrm_l(key.data(), text.c_str(), key.size(), locale);
    }
    key.resize(made);
    key.shrink_to_fit();
    return key;
}

// The name as the collation compares it: a digit run as its length's length, its length and its
// value, so numbers compare by value; a dot as "0", before letters and numbers so written.
std::string spelled(std::string_view name, bool fold_case) {
    std::string text;
    for (std::size_t i = 0; i < name.size();) {
        if (name[i] == '.') {
            text += '0';
            ++i;
            continue;
        }
        if (is_digit(name[i])) {
            std::size_t start = i;
            while (start < name.size() && name[start] == '0') {
                ++start;
            }
            std::size_t stop = start;
            while (stop < name.size() && is_digit(name[stop])) {
                ++stop;
            }
            const std::string_view value = start == stop ? "0" : name.substr(start, stop - start);
            const std::string length = std::to_string(value.size());
            text += static_cast<char>('0' + std::min<std::size_t>(length.size(), 9));
            text += length;
            text += value;
            i = stop;
            continue;
        }
        text += fold_case ? fold(name[i]) : name[i];
        ++i;
    }
    return text;
}

}  // namespace

bool set_collation(const char* locale) {
    locale_t made = newlocale(LC_COLLATE_MASK, locale, nullptr);
    if (made == nullptr) {
        return false;
    }
    // A collation that puts "B" before "a" orders code points (C, C.UTF-8): compare bytes instead.
    if (transformed("B", made) < transformed("a", made)) {
        freelocale(made);
        made = nullptr;
    }
    // Kept, never freed: another thread may still be making a key with an older one.
    static std::mutex mutex;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): never destroyed, so reachable at exit
    static auto* const kMadeBefore = new std::vector<locale_t>;
    const std::scoped_lock lock{mutex};
    if (made != nullptr) {
        kMadeBefore->push_back(made);
    }
    g_collation.store(made);
    return true;
}

std::string make_sort_key(std::string_view name) {
    const locale_t locale = g_collation.load();
    if (locale == nullptr) {
        std::string key = spelled(name, true);
        key.shrink_to_fit();
        return key;
    }
    return transformed(spelled(name, false), locale);
}

int natural_compare(std::string_view a, std::string_view b) {
    if (const int keys = sign(make_sort_key(a).compare(make_sort_key(b))); keys != 0) {
        return keys;
    }
    return sign(a.compare(b));
}

int compare_names(const Entry& a, const Entry& b) {
    if (a.sort_key.empty() || b.sort_key.empty()) {
        return natural_compare(a.name, b.name);
    }
    if (const int keys = sign(a.sort_key.compare(b.sort_key)); keys != 0) {
        return keys;
    }
    return sign(a.name.compare(b.name));
}

bool SortOrder::operator()(const Entry& a, const Entry& b) const {
    if (directories_first && a.is_directory() != b.is_directory()) {
        return a.is_directory();  // not reversed by descending
    }
    int result = 0;
    switch (key) {
        case SortKey::size:
            // A directory's size is its listing's, not its content's: directories keep their
            // name order among themselves.
            if (!a.is_directory() || !b.is_directory()) {
                result = three_way(a.size, b.size);
            }
            break;
        case SortKey::modified:
            result = three_way(a.modified_ns, b.modified_ns);
            break;
        case SortKey::type: {
            // A name that gives several types sorts by its best one (Entry::name_guess).
            const MimeId ta = a.mime != kUnknownMime ? a.mime : a.name_guess;
            const MimeId tb = b.mime != kUnknownMime ? b.mime : b.name_guess;
            if ((ta == kUnknownMime) != (tb == kUnknownMime)) {
                result = ta == kUnknownMime ? 1 : -1;
            } else if (mime != nullptr) {
                result = sign(mime->name(ta).compare(mime->name(tb)));
            } else {
                result = three_way(ta, tb);
            }
            break;
        }
        case SortKey::name:
            break;
    }
    if (result == 0) {
        result = compare_names(a, b);
    }
    return descending ? result > 0 : result < 0;
}

void Listing::merge(std::vector<Entry> chunk) {
    std::ranges::sort(chunk, order_);
    const auto middle = static_cast<std::ptrdiff_t>(entries_.size());
    entries_.insert(entries_.end(), std::make_move_iterator(chunk.begin()),
                    std::make_move_iterator(chunk.end()));
    std::ranges::inplace_merge(entries_, entries_.begin() + middle, order_);
}

void Listing::add(std::vector<Entry> chunk) {
    if (!show_hidden_) {
        const auto out = std::ranges::stable_partition(
            chunk, [this](const Entry& entry) { return !left_out(entry); });
        hidden_.insert(hidden_.end(), std::make_move_iterator(out.begin()),
                       std::make_move_iterator(out.end()));
        chunk.erase(out.begin(), out.end());
    }
    merge(std::move(chunk));
}

void Listing::replace(const std::unordered_set<std::string>& remove, std::vector<Entry> insert) {
    std::unordered_set<std::string_view> replaced;
    replaced.reserve(insert.size());
    for (const Entry& entry : insert) {
        replaced.insert(entry.name);
    }
    const auto gone = [&](const Entry& entry) {
        return remove.contains(entry.name) || replaced.contains(entry.name);
    };
    std::erase_if(entries_, gone);
    std::erase_if(hidden_, gone);
    add(std::move(insert));
}

void Listing::set_order(SortOrder order) {
    order_ = order;
    std::ranges::sort(entries_, order_);
}

Listing::Moved Listing::set_filter(bool show_hidden, std::string keep) {
    show_hidden_ = show_hidden;
    keep_ = std::move(keep);
    return refilter();
}

Listing::Moved Listing::set_only(Only only) {
    only_ = std::move(only);
    return refilter();
}

Listing::Moved Listing::refilter() {
    Moved moved;
    const auto back =
        std::ranges::partition(hidden_, [this](const Entry& entry) { return left_out(entry); });
    std::vector<Entry> shown(std::make_move_iterator(back.begin()),
                             std::make_move_iterator(back.end()));
    hidden_.erase(back.begin(), back.end());
    const auto out = std::ranges::stable_partition(
        entries_, [this](const Entry& entry) { return !left_out(entry); });
    for (Entry& entry : out) {
        moved.hidden.push_back(entry.name);
        hidden_.push_back(std::move(entry));
    }
    entries_.erase(out.begin(), out.end());
    for (const Entry& entry : shown) {
        moved.shown.push_back(entry.name);
    }
    merge(std::move(shown));
    return moved;
}

}  // namespace nordend::fs
