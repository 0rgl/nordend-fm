#pragma once

// The readers of each format, over the pieces in stream.hpp. A listing is filled as it goes, so
// that what was read before a Problem is thrown stays in it.

#include <cstdint>
#include <span>

#include "nordend/archive/archive.hpp"
#include "stream.hpp"

namespace nordend::formats::archive::detail {

// zip: the central directory, checked against each entry's local header; entries that overlap
// (a "zip bomb" sharing its data) or reach into the directory are refused.
void list_zip(const File& file, const nordend::archive::Limits& limits,
              nordend::archive::Listing& listing);

// The bytes of a zip entry (its locator: its central directory record), checked against its
// size and CRC-32.
void read_zip(const File& file, const nordend::archive::Entry& entry, Sink& sink);

// tar (ustar, GNU long names, pax), read in order from stream; locators are data offsets in it.
void list_tar(Stream& stream, const nordend::archive::Limits& limits,
              nordend::archive::Listing& listing);

// A 512-byte block that is a tar header (its checksum right).
[[nodiscard]] bool looks_like_tar(std::span<const std::uint8_t> block);

}  // namespace nordend::formats::archive::detail
