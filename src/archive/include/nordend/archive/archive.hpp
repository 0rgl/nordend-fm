#pragma once

// What Nordend knows of an archive (ROADMAP 0.9.0): its entries as the engine lists them, and
// why one cannot be read. Plain data: the readers are in formats/archive, run by the base engine
// in its cage; Nordend never parses an archive itself.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nordend::archive {

enum class Format : std::uint8_t {
    unknown = 0,
    zip = 1,
    tar = 2,
    tar_gzip = 3,  // a tar inside gzip (.tar.gz, .tgz)
    gzip = 4,      // one file compressed with gzip (.gz): one entry
};

enum class Type : std::uint8_t {
    file = 0,
    directory = 1,
    symlink = 2,   // link holds where it points
    hardlink = 3,  // link holds the entry it is the same file as
    other = 4,     // a device, a fifo, a sparse file...: listed, never extracted
};

// Why an entry's bytes cannot be had (none: they can).
enum class Unreadable : std::uint8_t {
    none = 0,
    encrypted = 1,  // a password protects it
    method = 2,     // compressed in a way Nordend does not read yet (bzip2, LZMA, zstd, xz...)
    kind = 3,       // a kind of entry with no bytes to read (Type::other)
};

struct Entry {
    std::string path;  // as the archive writes it, in UTF-8 (old zip names converted from CP437)
    std::string link;  // symlink: its target; hardlink: the entry's path
    Type type{Type::file};
    Unreadable unreadable{Unreadable::none};
    std::uint32_t mode{0};     // permission bits (07777); 0 when the archive says nothing
    std::int64_t mtime{0};     // seconds since 1970 (UTC); 0 when the archive says nothing
    std::uint64_t size{0};     // its bytes once read (a .gz's from its trailer, modulo 4 GiB)
    std::uint64_t packed{0};   // its bytes in the archive
    std::uint64_t locator{0};  // where the engine finds it again; Nordend only gives it back

    bool operator==(const Entry&) const = default;
};

enum class Failure : std::uint8_t {
    none = 0,
    not_archive = 1,  // no format Nordend reads
    damaged = 2,      // a format it reads, broken or made to deceive (detail says how)
    too_large = 3,    // more entries, or longer names, than the limits
    unreadable = 4,   // the file could not be read
};

struct Limits {
    std::uint32_t max_entries{1'000'000};
    std::uint32_t max_path_bytes{4096};
};

struct Listing {
    Format format{Format::unknown};
    Failure failure{Failure::none};
    std::string detail;  // why, for a person
    std::vector<Entry> entries;
};

// "zip", "tar", "tar.gz", "gzip": for messages and logs.
[[nodiscard]] std::string_view format_name(Format format);

// Reading archives goes through the engine that reads them (isolate::use_engines sets these;
// tests set in-process ones). Without one, every archive is "unavailable".
using Lister = std::function<Listing(const std::filesystem::path& archive, const Limits& limits)>;

struct Read {
    Failure failure{Failure::none};
    std::string detail;
    std::uint64_t bytes{0};  // written to out
};

// Writes entry's bytes (entry as the listing gave it) to out, at most max_bytes of them.
using EntryReader = std::function<Read(const std::filesystem::path& archive, Format format,
                                       const Entry& entry, int out, std::uint64_t max_bytes)>;

void set_lister(Lister lister);
void set_entry_reader(EntryReader reader);

// Thread-safe; an unreadable failure ("no engine reads archives") when none is set.
[[nodiscard]] Listing list_archive(const std::filesystem::path& archive, const Limits& limits = {});
[[nodiscard]] Read read_archive_entry(const std::filesystem::path& archive, Format format,
                                      const Entry& entry, int out, std::uint64_t max_bytes);

}  // namespace nordend::archive
