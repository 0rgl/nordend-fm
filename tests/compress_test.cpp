// Making archives (ROADMAP 0.9.0, step 5): run_compress read back by Nordend's own engine (in this
// process, tests/main.cpp), by unzip and GNU tar when they are installed, and extracted again.
#include "nordend/fs/compress.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/archive/archive.hpp"
#include "nordend/core/event_loop.hpp"
#include "nordend/core/worker_pool.hpp"
#include "nordend/fs/archive_folder.hpp"
#include "nordend/fs/directory_watcher.hpp"
#include "nordend/fs/extract.hpp"
#include "nordend/fs/mime.hpp"
#include "nordend/fs/transfer.hpp"
#include "nordend/views/browser.hpp"
#include "temp_dir.hpp"

using nordend::archive::Entry;
using nordend::archive::Format;
using nordend::archive::Type;
using nordend::fs::run_compress;
using nordend::fs::TransferKind;
using nordend::fs::TransferResult;
using nordend::test::TempDir;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        return {};
    }
    std::ifstream in{path, std::ios::binary};
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void write_file(const fs::path& path, const std::string& bytes) {
    std::ofstream out{path, std::ios::binary};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// tree/{a.txt, città.txt, run.sh (0755), big.bin (300 kB), empty/, deep/<long name>.txt, link ->
// a.txt}
fs::path make_tree(const fs::path& root) {
    const fs::path tree = root / "tree";
    fs::create_directories(tree / "empty");
    fs::create_directories(tree / "deep");
    write_file(tree / "a.txt", "hello\n");
    write_file(tree / "città.txt", "accents\n");
    write_file(tree / "run.sh", "#!/bin/sh\n");
    fs::permissions(tree / "run.sh", fs::perms::owner_all | fs::perms::group_read |
                                         fs::perms::group_exec | fs::perms::others_read |
                                         fs::perms::others_exec);
    std::string big;
    for (int i = 0; i < 30000; ++i) {
        big += std::to_string(i * 7919) + "\n";  // compresses, but not to nothing
    }
    write_file(tree / "big.bin", big);
    write_file(tree / "deep" / (std::string(120, 'n') + ".txt"), "a long name\n");
    fs::create_symlink("a.txt", tree / "link");
    const timespec when{.tv_sec = 1794744000, .tv_nsec = 0};
    const std::array<timespec, 2> times{when, when};
    ::utimensat(AT_FDCWD, (tree / "a.txt").c_str(), times.data(), 0);
    return tree;
}

TransferResult compress(const fs::path& source, std::vector<std::string> names,
                        const fs::path& destination, std::string name, bool tar_gz,
                        const nordend::core::CancelToken& token = {}) {
    return run_compress({.kind = TransferKind::compress,
                         .source = source,
                         .names = std::move(names),
                         .destination = destination,
                         .force_copy = false,
                         .archive_name = std::move(name),
                         .tar_gz = tar_gz},
                        token, {});
}

const Entry* find(const nordend::archive::Listing& listing, std::string_view path) {
    const auto it = std::ranges::find(listing.entries, path, &Entry::path);
    return it == listing.entries.end() ? nullptr : &*it;
}

// The bytes of an entry, read back by the engine.
std::string bytes_of(const fs::path& archive, Format format, const Entry& entry) {
    const int out = ::memfd_create("compress-test", MFD_CLOEXEC);
    REQUIRE(out >= 0);
    const nordend::archive::Read read =
        nordend::archive::read_archive_entry(archive, format, entry, out, UINT64_MAX);
    CHECK(read.failure == nordend::archive::Failure::none);
    std::string bytes(read.bytes, '\0');
    CHECK(::pread(out, bytes.data(), bytes.size(), 0) == static_cast<ssize_t>(bytes.size()));
    ::close(out);
    return bytes;
}

bool have(const char* program) {
    const std::string command = std::string{"command -v "} + program + " > /dev/null 2>&1";
    return std::system(command.c_str()) == 0;  // NOLINT(cert-env33-c): a test, fixed words
}

int run(const std::string& command) {
    return std::system(command.c_str());  // NOLINT(cert-env33-c): a test, paths quoted
}

std::string quoted(const fs::path& path) { return "'" + path.string() + "'"; }

bool no_temporary(const fs::path& folder) {
    return std::ranges::none_of(fs::directory_iterator{folder}, [](const fs::directory_entry& e) {
        return e.path().filename().string().starts_with(".nordend-part-");
    });
}

bool run_until(nordend::core::EventLoop& loop, const std::function<bool()>& done) {
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

}  // namespace

TEST_CASE("compress: names proposed for an archive") {
    using nordend::fs::archive_name_for;
    CHECK(archive_name_for({"Trip"}, true, false) == "Trip.zip");
    CHECK(archive_name_for({"photo.jpg"}, false, true) == "photo.tar.gz");
    CHECK(archive_name_for({"v1.2 notes"}, true, false) == "v1.2 notes.zip");
    CHECK(archive_name_for({".bashrc"}, false, false) == ".bashrc.zip");
    CHECK(archive_name_for({"a", "b"}, false, true) == "Archive.tar.gz");
}

TEST_CASE("compress: a zip and a tar.gz read back as they were made") {
    TempDir dir;
    const fs::path tree = make_tree(dir.path);
    for (const bool tar_gz : {false, true}) {
        CAPTURE(tar_gz);
        const std::string name = tar_gz ? "tree.tar.gz" : "tree.zip";
        const TransferResult made = compress(dir.path, {"tree"}, dir.path, name, tar_gz);
        REQUIRE(made.refused.empty());
        CHECK(made.errors.empty());
        CHECK(made.placed == std::vector<std::string>{name});
        CHECK(made.files_done == 5);
        CHECK(no_temporary(dir.path));
        const fs::path archive = dir.path / name;
        nordend::fs::forget_archive_trees();
        const nordend::archive::Listing listing = nordend::archive::list_archive(archive);
        REQUIRE(listing.failure == nordend::archive::Failure::none);
        CHECK(listing.format == (tar_gz ? Format::tar_gzip : Format::zip));
        const Entry* a = find(listing, "tree/a.txt");
        REQUIRE(a != nullptr);
        CHECK(a->mtime == 1794744000);
        CHECK(bytes_of(archive, listing.format, *a) == "hello\n");
        const Entry* big = find(listing, "tree/big.bin");
        REQUIRE(big != nullptr);
        CHECK(bytes_of(archive, listing.format, *big) == read_file(tree / "big.bin"));
        if (!tar_gz) {
            CHECK(big->packed < big->size);  // each zip entry deflated (a tar.gz: the whole)
        }
        const Entry* accents = find(listing, "tree/città.txt");
        REQUIRE(accents != nullptr);
        const Entry* run_sh = find(listing, "tree/run.sh");
        REQUIRE(run_sh != nullptr);
        CHECK(run_sh->mode == 0755);
        const Entry* empty = find(listing, "tree/empty/");
        REQUIRE(empty != nullptr);
        CHECK(empty->type == Type::directory);
        const Entry* link = find(listing, "tree/link");
        REQUIRE(link != nullptr);
        CHECK(link->type == Type::symlink);
        CHECK(link->link == "a.txt");
        CHECK(find(listing, "tree/deep/" + std::string(120, 'n') + ".txt") != nullptr);

        // Extracted again by Nordend: the same files.
        const fs::path out = dir.path / (tar_gz ? "out-tar" : "out-zip");
        fs::create_directories(out);
        const TransferResult back = nordend::fs::run_extract({.kind = TransferKind::copy,
                                                              .source = archive,
                                                              .names = {"tree"},
                                                              .destination = out,
                                                              .force_copy = false},
                                                             {}, {});
        CHECK(back.errors.empty());
        CHECK(read_file(out / "tree" / "big.bin") == read_file(tree / "big.bin"));
        CHECK(read_file(out / "tree" / "città.txt") == "accents\n");
        CHECK(fs::is_empty(out / "tree" / "empty"));
        CHECK(fs::read_symlink(out / "tree" / "link") == "a.txt");

        // And by the tools everyone has.
        if (!tar_gz && have("unzip")) {
            CHECK(run("unzip -tq " + quoted(archive) + " > /dev/null") == 0);
        }
        if (tar_gz && have("tar")) {
            const fs::path by_tar = dir.path / "by-tar";
            fs::create_directories(by_tar);
            CHECK(run("tar xzf " + quoted(archive) + " -C " + quoted(by_tar)) == 0);
            CHECK(read_file(by_tar / "tree" / "big.bin") == read_file(tree / "big.bin"));
            CHECK(read_file(by_tar / "tree" / "deep" / (std::string(120, 'n') + ".txt")) ==
                  "a long name\n");
            CHECK(fs::read_symlink(by_tar / "tree" / "link") == "a.txt");
        }
    }
}

TEST_CASE("compress: a name taken gets a number before its ending; nothing replaced") {
    TempDir dir;
    make_tree(dir.path);
    write_file(dir.path / "tree.zip", "mine");
    write_file(dir.path / "tree.tar.gz", "mine too");
    CHECK(compress(dir.path, {"tree"}, dir.path, "tree.zip", false).placed ==
          std::vector<std::string>{"tree 2.zip"});
    CHECK(compress(dir.path, {"tree"}, dir.path, "tree.tar.gz", true).placed ==
          std::vector<std::string>{"tree 2.tar.gz"});
    CHECK(read_file(dir.path / "tree.zip") == "mine");
    CHECK(read_file(dir.path / "tree.tar.gz") == "mine too");
}

TEST_CASE("compress: what is not a file is said and left out; stopped, nothing is left") {
    TempDir dir;
    make_tree(dir.path);
    REQUIRE(::mkfifo((dir.path / "tree" / "pipe").c_str(), 0644) == 0);
    const TransferResult made = compress(dir.path, {"tree"}, dir.path, "t.zip", false);
    CHECK(made.errors == std::vector<std::string>{"tree/pipe: not a file, left out"});
    CHECK(made.placed == std::vector<std::string>{"t.zip"});

    nordend::core::CancelToken token;
    token.cancel();
    const TransferResult stopped = compress(dir.path, {"tree"}, dir.path, "s.zip", false, token);
    CHECK(stopped.cancelled);
    CHECK_FALSE(fs::exists(dir.path / "s.zip"));
    CHECK(no_temporary(dir.path));
    CHECK(compress(dir.path, {"tree"}, dir.path, "a/b.zip", false).refused ==
          "No name for the archive");
}

TEST_CASE("compress: more than 65535 entries make a ZIP64 directory, read back whole") {
    TempDir dir;
    const fs::path many = dir.path / "many";
    fs::create_directories(many);
    constexpr int files = 66000;
    for (int i = 0; i < files; ++i) {
        std::ofstream{many / std::to_string(i)};
    }
    const TransferResult made = compress(dir.path, {"many"}, dir.path, "many.zip", false);
    REQUIRE(made.refused.empty());
    nordend::fs::forget_archive_trees();
    const nordend::archive::Listing listing = nordend::archive::list_archive(dir.path / "many.zip");
    CHECK(listing.failure == nordend::archive::Failure::none);
    CHECK(listing.entries.size() == files + 1);  // the folder too
    if (have("unzip")) {
        CHECK(run("unzip -tq " + quoted(dir.path / "many.zip") + " > /dev/null") == 0);
    }
}

TEST_CASE("compress: from the Browser, renamed at once") {
    TempDir dir;
    make_tree(dir.path);
    nordend::core::EventLoop loop;
    nordend::core::WorkerPool pool{loop, 2};
    nordend::fs::DirectoryWatcher watcher{loop};
    nordend::views::Browser browser{loop,    pool,
                                    watcher, nordend::fs::MimeDatabase::load(),
                                    [] {},   [](nordend::ui::CursorShape /*shape*/) {}};
    browser.set_home_trash(dir.path / "Trash");
    browser.go_to(dir.path, "tree");
    REQUIRE(run_until(loop, [&] { return !browser.loading(); }));
    browser.compress(true);
    REQUIRE(run_until(loop, [&] {
        return fs::exists(dir.path / "tree.tar.gz") && !browser.status_bar().transfer();
    }));
    CHECK(browser.status_bar().message() == "Compressed 5 files into “tree.tar.gz”");
    CHECK(browser.name_editor().active());
}
