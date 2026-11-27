#include <algorithm>
#include <cstddef>
#include <iterator>
#include <sys/stat.h>

#include "nordend/fs/sort.hpp"
#include "nordend/preview/preview.hpp"

namespace nordend::preview {

Archive read_archive(const std::filesystem::path& path,
                     const std::shared_ptr<const fs::MimeDatabase>& types, std::size_t top) {
    const std::shared_ptr<const fs::ArchiveTree> tree = fs::archive_tree(path, types.get());
    Archive out;
    out.format = tree->format();
    out.failure = tree->failure();
    out.detail = tree->detail();
    out.totals = tree->totals();
    out.left_out = tree->left_out();
    if (const std::vector<fs::Entry>* const entries = tree->folder(""); entries != nullptr) {
        out.top_count = entries->size();
        const std::size_t count = std::min(top, entries->size());
        if (count > 0) {
            // Folders first, then by name, as a folder shows them; each key made once.
            std::vector<fs::Entry> order(entries->begin(), entries->end());
            for (fs::Entry& entry : order) {
                entry.sort_key = fs::make_sort_key(entry.name);
            }
            const auto end = order.begin() + static_cast<std::ptrdiff_t>(count);
            std::ranges::partial_sort(order, end, fs::SortOrder{});
            out.top.assign(std::make_move_iterator(order.begin()), std::make_move_iterator(end));
        }
    }
    return out;
}

}  // namespace nordend::preview
