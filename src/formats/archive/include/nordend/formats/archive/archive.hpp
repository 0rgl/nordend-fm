#pragma once

// The archive family (ROADMAP 0.9.0): zip, tar, tar.gz and single .gz files, read through the
// descriptor they were opened as. Linked into the isolated helper (and tests and development
// tools), never into Nordend. Every number an archive gives is checked before it is used; an
// entry never yields more bytes than it says it holds.

#include <cstdint>

#include "nordend/archive/archive.hpp"

namespace nordend::formats::archive {

// The archive's format and entries, in the archive's order. A damaged archive keeps the entries
// read before the damage, with the failure saying what is wrong.
[[nodiscard]] nordend::archive::Listing list(int fd, const nordend::archive::Limits& limits);

// Writes the bytes of entry (as list gave it, in an archive of format) to out, at most
// max_bytes of them: more is a too_large failure. fd and out stay the caller's.
[[nodiscard]] nordend::archive::Read read_entry(int fd, nordend::archive::Format format,
                                                const nordend::archive::Entry& entry, int out,
                                                std::uint64_t max_bytes);

}  // namespace nordend::formats::archive
