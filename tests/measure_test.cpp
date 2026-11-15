#include "nordend/fs/measure.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <unistd.h>

#include <doctest/doctest.h>

#include "nordend/core/worker_pool.hpp"
#include "temp_dir.hpp"

using nordend::core::CancelToken;
using nordend::fs::MeasureOptions;
using nordend::fs::Measurer;
using nordend::fs::Tally;
using nordend::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("measure adds up a tree: files, folders, links, a hard link once") {
    const TempDir dir;
    const fs::path root = dir.path / "Trip";
    fs::create_directories(root / "Day 1" / "raw");
    dir.write("Trip/notes.txt", "12345");
    dir.write("Trip/Day 1/P1.ORF", std::string(1000, 'x'));
    dir.write("Trip/Day 1/raw/P2.ORF", std::string(300, 'y'));
    fs::create_hard_link(root / "Day 1" / "P1.ORF", root / "P1 again.ORF");
    fs::create_symlink(root / "Day 1", root / "link to day");  // not followed
    fs::create_symlink("missing", root / "dangling");

    const Tally tally = nordend::fs::measure(root, {}, CancelToken{});
    CHECK(tally.files == 4);
    CHECK(tally.folders == 3);  // Trip, Day 1, raw
    CHECK(tally.others == 2);
    CHECK(tally.entries() == 9);
    CHECK(tally.bytes == 5 + 1000 + 300);
    CHECK(tally.complete());
}

TEST_CASE("measure follows a link given as the root, never one inside") {
    const TempDir dir;
    fs::create_directories(dir.path / "real");
    dir.write("real/a", "abc");
    fs::create_directory_symlink(dir.path / "real", dir.path / "link");

    const Tally followed = nordend::fs::measure(dir.path / "link", {}, CancelToken{});
    CHECK(followed.folders == 1);
    CHECK(followed.files == 1);
    CHECK(followed.bytes == 3);

    MeasureOptions options;
    options.follow_root = false;
    const Tally link = nordend::fs::measure(dir.path / "link", options, CancelToken{});
    CHECK(link.others == 1);
    CHECK(link.entries() == 1);
    CHECK(link.bytes == 0);
}

TEST_CASE("measure of a file, and of nothing") {
    const TempDir dir;
    const Tally file = nordend::fs::measure(dir.write("one", "1234"), {}, CancelToken{});
    CHECK(file.files == 1);
    CHECK(file.bytes == 4);
    const Tally missing = nordend::fs::measure(dir.path / "missing", {}, CancelToken{});
    CHECK(missing.entries() == 0);
}

TEST_CASE("measure stops at max_depth and says so") {
    const TempDir dir;
    fs::create_directories(dir.path / "a" / "b" / "c");
    dir.write("a/b/c/deep", "123");
    dir.write("a/top", "1");
    MeasureOptions options;
    options.max_depth = 2;  // a (0), b (1) entered; c (2) counted, not entered
    const Tally tally = nordend::fs::measure(dir.path / "a", options, CancelToken{});
    CHECK(tally.folders == 3);
    CHECK(tally.files == 1);
    CHECK(tally.bytes == 1);
    CHECK(tally.not_entered == 1);
    CHECK_FALSE(tally.complete());
}

TEST_CASE("measure counts a folder it cannot read as unreadable") {
    if (geteuid() == 0) {
        return;  // root reads everything
    }
    const TempDir dir;
    fs::create_directories(dir.path / "a" / "closed");
    dir.write("a/closed/inside", "123");
    fs::permissions(dir.path / "a" / "closed", fs::perms::none);
    const Tally tally = nordend::fs::measure(dir.path / "a", {}, CancelToken{});
    fs::permissions(dir.path / "a" / "closed", fs::perms::owner_all);
    CHECK(tally.folders == 2);
    CHECK(tally.files == 0);
    CHECK(tally.unreadable == 1);
    CHECK_FALSE(tally.complete());
}

TEST_CASE("measure stops when cancelled and tells its progress on the way") {
    const TempDir dir;
    for (int i = 0; i < 1000; ++i) {
        dir.write(std::format("f{}", i), "x");
    }
    const CancelToken token;
    MeasureOptions options;
    options.progress_interval = std::chrono::milliseconds{0};
    int told = 0;
    std::uint64_t last = 0;
    const Tally whole = nordend::fs::measure(dir.path, options, token, [&](const Tally& tally) {
        ++told;
        CHECK(tally.entries() > last);
        last = tally.entries();
    });
    CHECK(whole.files == 1000);
    CHECK(told == 3);  // at 256, 512 and 768 entries

    token.cancel();
    const Tally cancelled = nordend::fs::measure(dir.path, options, token);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(cancelled.complete());
}

TEST_CASE("measure sums several trees, a hard link between them once") {
    const TempDir dir;
    fs::create_directories(dir.path / "a");
    fs::create_directories(dir.path / "b");
    dir.write("a/photo", std::string(100, 'p'));
    fs::create_hard_link(dir.path / "a" / "photo", dir.path / "b" / "photo");
    const CancelToken token;
    Measurer measurer{{}, token};
    measurer.add(dir.path / "a");
    measurer.add(dir.path / "b");
    CHECK(measurer.tally().files == 2);
    CHECK(measurer.tally().bytes == 100);
}

TEST_CASE("is_remote: network and FUSE file systems, fuseblk is local") {
    CHECK(nordend::fs::is_remote_type(0x6969, "nfs4"));
    CHECK(nordend::fs::is_remote_type(0xFF534D42, "cifs"));
    CHECK(nordend::fs::is_remote_type(0xFE534D42, "smb3"));
    CHECK(nordend::fs::is_remote_type(0x65735546, "fuse.sshfs"));
    CHECK(nordend::fs::is_remote_type(0x65735546, "fuse.gvfsd-fuse"));
    CHECK_FALSE(nordend::fs::is_remote_type(0x65735546, "fuseblk"));  // NTFS, exFAT disks
    CHECK_FALSE(nordend::fs::is_remote_type(0xEF53, "ext4"));
    CHECK_FALSE(nordend::fs::is_remote_type(0x9123683E, "btrfs"));
    CHECK_FALSE(nordend::fs::is_remote_type(0x01021994, "tmpfs"));
    const TempDir dir;
    CHECK_FALSE(nordend::fs::is_remote(dir.path));
}
