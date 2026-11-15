#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nordend/fs/permissions.hpp"
#include "nordend/fs/transfer.hpp"

// What Ctrl+Z undoes (ROADMAP 6.7): the last operation on files, as it was done. Undoing one
// does its inverse, which is itself an operation: that one is what Ctrl+Shift+Z undoes again.
namespace nordend::views {

struct FileOperation {
    enum class Kind : std::uint8_t {
        rename,   // one item: from the old name to the new (its sidecars follow)
        create,   // one item: to = the new folder or file; from = its template, or empty
        copy,     // undone by trashing the copies
        move,     // undone by moving them back
        trash,    // to = where each went in the trash's files/: undone by restoring
        restore,  // undone by trashing again
        erase,    // cannot be undone
        // Allowed to run as a program, or no longer (items: from = to = each file; modes: its
        // bits before and after): undone by giving the bits before back
        permissions,
    };

    Kind kind{Kind::copy};
    std::vector<fs::PlacedItem> items;  // sidecars left out: they follow their photo
    std::vector<fs::ModeChange> modes;  // permissions only, one per item
    // Copies and moves: files put in the place of others, not in items (the old ones are gone);
    // the undo says so.
    std::size_t replaced{0};
};

// What the operation did, for "Undone: ..." and "Redone: ...": "“P1.ORF” is “Lake.ORF” again",
// "3 items moved to the trash", "“a.txt” restored", "2 items back in photos", "“tool” can run
// as a program", "2 items can no longer run as programs".
[[nodiscard]] std::string describe(const FileOperation& operation);

}  // namespace nordend::views
