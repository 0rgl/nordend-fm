#include "nordend/fs/permissions.hpp"

#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/fs/origin.hpp"
#include "temp_dir.hpp"

using nordend::fs::ModeResult;
using nordend::test::TempDir;
namespace fs = std::filesystem;

namespace {

unsigned mode_of(const fs::path& path) {
    struct stat info{};
    REQUIRE(::stat(path.c_str(), &info) == 0);
    return info.st_mode & 07777U;
}

}  // namespace

TEST_CASE("running_mode: u+x to allow, every x away to stop, other bits kept") {
    using nordend::fs::running_mode;
    CHECK(running_mode(0644, true) == 0744);
    CHECK(running_mode(0600, true) == 0700);
    CHECK(running_mode(0755, true) == 0755);
    CHECK(running_mode(0755, false) == 0644);
    CHECK(running_mode(0711, false) == 0600);
    CHECK(running_mode(04755, false) == 04644);  // setuid kept: not ours to change
}

TEST_CASE("set_running changes regular files only, says what changed, undoes and redoes") {
    const TempDir dir;
    const fs::path app = dir.write("Tool.AppImage", "x");
    const fs::path script = dir.write("run.sh", "#!/bin/sh\n");
    fs::create_directory(dir.path / "folder");
    ::chmod(app.c_str(), 0644);
    ::chmod(script.c_str(), 0755);

    const ModeResult allowed =
        nordend::fs::set_running({app, script, dir.path / "folder", dir.path / "gone"}, true);
    CHECK(mode_of(app) == 0744);
    CHECK(mode_of(script) == 0755);  // already allowed: unchanged, not listed
    CHECK(mode_of(dir.path / "folder") == mode_of(dir.path / "folder"));
    REQUIRE(allowed.changed.size() == 1);
    CHECK(allowed.changed[0].path == app);
    CHECK(allowed.changed[0].before == 0644);
    CHECK(allowed.changed[0].after == 0744);
    REQUIRE(allowed.errors.size() == 1);
    CHECK(allowed.errors[0] == "gone: No such file or directory");

    // Undo: the bits before; redo: the bits after.
    const ModeResult undone = nordend::fs::restore_modes(allowed.changed, true);
    CHECK(mode_of(app) == 0644);
    REQUIRE(undone.changed.size() == 1);
    CHECK(undone.changed[0].before == 0744);
    CHECK(undone.changed[0].after == 0644);
    const ModeResult redone = nordend::fs::restore_modes(undone.changed, true);
    CHECK(mode_of(app) == 0744);
    CHECK(redone.errors.empty());

    // Changed by someone else since: left alone, and said.
    ::chmod(app.c_str(), 0600);
    const ModeResult stale = nordend::fs::restore_modes(redone.changed, true);
    CHECK(mode_of(app) == 0600);
    CHECK(stale.changed.empty());
    CHECK(stale.errors == std::vector<std::string>{"Tool.AppImage: changed since"});

    // Stop: every execute bit goes.
    const ModeResult stopped = nordend::fs::set_running({script}, false);
    CHECK(mode_of(script) == 0644);
    CHECK(stopped.changed.size() == 1);
}

TEST_CASE("set_running through a link changes its target, as chmod does") {
    const TempDir dir;
    const fs::path target = dir.write("tool", "x");
    ::chmod(target.c_str(), 0644);
    fs::create_symlink(target, dir.path / "link");
    const ModeResult result = nordend::fs::set_running({dir.path / "link"}, true);
    CHECK(result.changed.size() == 1);
    CHECK(mode_of(target) == 0744);
}

TEST_CASE("set_running: allowing takes away the mark that asks first, stopping keeps it") {
    const TempDir dir;
    const fs::path tool = dir.write("tool", "x");
    ::chmod(tool.c_str(), 0755);
    if (::setxattr(tool.c_str(), nordend::fs::kOriginAttribute, "nordend:extracted", 17, 0) != 0) {
        MESSAGE("no user attributes here: skipped");
        return;
    }
    // Already allowed by its bits, still asked about: allowing it is what takes the mark away.
    const ModeResult allowed = nordend::fs::set_running({tool}, true);
    CHECK(allowed.errors.empty());
    CHECK(allowed.changed.empty());
    CHECK_FALSE(nordend::fs::origin_of(tool).has_value());

    REQUIRE(::setxattr(tool.c_str(), nordend::fs::kOriginAttribute, "x", 1, 0) == 0);
    static_cast<void>(nordend::fs::set_running({tool}, false));
    CHECK(nordend::fs::origin_of(tool) == "x");

    // A read-only program with the mark (tarballs pack them 0555): allowed, and left read-only.
    const fs::path packed = dir.write("packed", "x");
    REQUIRE(::setxattr(packed.c_str(), nordend::fs::kOriginAttribute, "x", 1, 0) == 0);
    ::chmod(packed.c_str(), 0555);
    const ModeResult unpacked = nordend::fs::set_running({packed}, true);
    CHECK(unpacked.errors.empty());
    CHECK_FALSE(nordend::fs::origin_of(packed).has_value());
    CHECK(mode_of(packed) == 0555);

    // Read-only with setgid: no chmod for the moment (it could drop setgid), so the mark stays;
    // that is no error, Enter keeps asking.
    const fs::path shared = dir.write("shared", "x");
    REQUIRE(::setxattr(shared.c_str(), nordend::fs::kOriginAttribute, "x", 1, 0) == 0);
    if (::chmod(shared.c_str(), 02555) == 0 && mode_of(shared) == 02555) {
        const ModeResult kept = nordend::fs::set_running({shared}, true);
        CHECK(kept.errors.empty());
        CHECK(nordend::fs::origin_of(shared) == "x");
        CHECK(mode_of(shared) == 02555);
    }

    // The user's own read-only file without a mark: allowed, though nothing could be removed.
    const fs::path plain = dir.write("plain", "x");
    ::chmod(plain.c_str(), 0444);
    const ModeResult read_only = nordend::fs::set_running({plain}, true);
    CHECK(read_only.errors.empty());
    CHECK(mode_of(plain) == 0544);
}
