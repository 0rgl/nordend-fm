// org.freedesktop.FileManager1 (issue #16): the arguments nordend gets, the service on a private
// bus with a made-up program in place of nordend, and the user's service file.
#include "nordend/portal/file_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sdbus-c++/sdbus-c++.h>
#include <sstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "nordend/core/event_loop.hpp"
#include "nordend/core/file.hpp"
#include "nordend/platform/bus_loop.hpp"
#include "nordend/platform/desktop_entry.hpp"
#include "nordend/platform/file_manager_config.hpp"
#include "private_bus.hpp"
#include "temp_dir.hpp"

namespace portal = nordend::portal;
namespace platform = nordend::platform;
using nordend::core::EventLoop;
using nordend::platform::BusLoop;
using nordend::test::PrivateBus;
using nordend::test::TempDir;
using Strings = std::vector<std::string>;
using namespace std::chrono_literals;

namespace {

bool run_until(EventLoop& loop, const std::function<bool()>& done) {
    const auto poll = loop.add_timer(1ms, 1ms, [&] {
        if (done()) {
            loop.quit(0);
        }
    });
    const auto watchdog = loop.add_timer(5s, 0ms, [&loop] { loop.quit(1); });
    const int code = loop.run();
    loop.remove(poll);
    loop.remove(watchdog);
    return code == 0;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path};
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("file manager: the arguments for nordend, local files only, in their order") {
    using portal::Show;
    CHECK(portal::show_arguments(Show::items, {"file:///home/u/Downloads/a%20b.zip"}) ==
          Strings{"--reveal", "--", "/home/u/Downloads/a b.zip"});
    CHECK(portal::show_arguments(Show::properties, {"file:///a", "file://localhost/b/c"}) ==
          Strings{"--properties", "--", "/a", "/b/c"});
    CHECK(portal::show_arguments(Show::folders, {"file:///srv/photos/"}) ==
          Strings{"--", "/srv/photos"});
    // Another host, another scheme, a relative path: left out; nothing left: nothing to start.
    CHECK(portal::show_arguments(Show::items, {"sftp://host/a", "file://host/a", "a.txt"}).empty());
    CHECK(portal::show_arguments(Show::items, {}).empty());
    CHECK(portal::show_arguments(Show::items, {"https://x/y", "file:///-dash"}) ==
          Strings{"--reveal", "--", "/-dash"});
}

namespace {

// The made-up nordend's records written so far: one "token=" line each.
std::size_t count_records(const std::string& calls) {
    std::size_t count = 0;
    for (std::size_t at = calls.find("token="); at != std::string::npos;
         at = calls.find("token=", at + 1)) {
        ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("file manager: on the bus, each call answers at once and starts nordend once") {
    TempDir dir;
    const std::filesystem::path socket = dir.path / "bus";
    PrivateBus daemon{socket};
    if (!daemon.running()) {
        MESSAGE("no dbus-daemon: skipped");
        return;
    }
    // The made-up nordend: its arguments and its activation token, one line each, appended in one
    // write (two started at once must not mix their lines; which comes first is not known).
    const std::filesystem::path program = dir.path / "nordend";
    {
        std::ofstream out{program};
        out << "#!/bin/sh\nrecord=$(printf '%s\\n' \"$@\"; echo \"token=$XDG_ACTIVATION_TOKEN\")\n"
            << R"(printf '%s\n' "$record" >> ')" << (dir.path / "calls").string() << "'\n";
    }
    std::filesystem::permissions(program, std::filesystem::perms::owner_all);

    EventLoop loop;
    const std::string address = "unix:path=" + socket.string();
    const auto service = sdbus::createSessionBusConnectionWithAddress(address);
    BusLoop service_bus{loop, *service, "service"};
    portal::FileManager manager{loop, *service, service_bus, program};
    bool idle = false;
    manager.set_idle_handler(100ms, [&idle] { idle = true; });
    service->requestName(sdbus::ServiceName{std::string{portal::kFileManagerName}});
    service_bus.pump();
    const auto client = sdbus::createSessionBusConnectionWithAddress(address);
    BusLoop client_bus{loop, *client, "client"};
    const auto proxy =
        sdbus::createProxy(*client, sdbus::ServiceName{std::string{portal::kFileManagerName}},
                           sdbus::ObjectPath{std::string{portal::kFileManagerPath}});

    const auto call = [&](const char* method, const Strings& uris, const std::string& token) {
        auto replied = std::make_shared<std::optional<bool>>();
        proxy->callMethodAsync(method)
            .onInterface(std::string{portal::kFileManagerInterface})
            .withArguments(uris, token)
            // By value: sdbus-c++ 2.2 (Nix) tells the error from a reply value by its type.
            .uponReplyInvoke(
                [replied](
                    // NOLINTNEXTLINE(performance-unnecessary-value-param)
                    std::optional<sdbus::Error> error) { *replied = !error.has_value(); });
        client_bus.pump();
        return replied;
    };

    const auto items = call("ShowItems", {"file:///tmp/a%20b.zip"}, "TOKEN-1");
    REQUIRE(run_until(loop, [&] { return items->has_value() && manager.started() == 1; }));
    CHECK(items->value_or(false));
    const auto folders = call("ShowFolders", {"file:///tmp"}, "");
    const auto properties = call("ShowItemProperties", {"file:///etc/hostname"}, "TOKEN-2");
    const auto nothing = call("ShowItems", {"https://example.org/a"}, "");
    REQUIRE(run_until(loop, [&] {
        return folders->has_value() && properties->has_value() && nothing->has_value() &&
               manager.started() == 3 &&
               count_records(read_file(dir.path / "calls")) == 3;  // every start has written
    }));
    CHECK(nothing->value_or(false));  // answered, nothing started
    // Each start's record (ending with its token), in whichever order they came.
    std::vector<std::string> records;
    const std::string calls = read_file(dir.path / "calls");
    for (std::size_t at = 0; at < calls.size();) {
        const std::size_t token = calls.find("token=", at);
        const std::size_t end = token == std::string::npos ? calls.size() : calls.find('\n', token);
        const std::size_t stop = end == std::string::npos ? calls.size() : end + 1;
        records.push_back(calls.substr(at, stop - at));
        at = stop;
    }
    std::ranges::sort(records);
    std::vector<std::string> expected{"--reveal\n--\n/tmp/a b.zip\ntoken=TOKEN-1\n",
                                      "--\n/tmp\ntoken=\n",
                                      "--properties\n--\n/etc/hostname\ntoken=TOKEN-2\n"};
    std::ranges::sort(expected);
    CHECK(records == expected);
    CHECK_FALSE(idle);
    REQUIRE(run_until(loop, [&] { return idle; }));  // and gone when nothing more is asked
}

TEST_CASE("file manager: the service file is written, said, and removed only when ours") {
    TempDir dir;
    platform::Environment environment;
    environment.data_dirs = {dir.path / "share", "/usr/share"};
    const std::filesystem::path service = platform::file_manager_service_path(environment);
    CHECK(service == dir.path / "share/dbus-1/services/org.freedesktop.FileManager1.service");

    const std::string text = platform::file_manager_service("/usr/libexec/nordend/nordend-portal");
    CHECK(platform::is_ours(text));
    CHECK(text.find("Name=org.freedesktop.FileManager1\n") != std::string::npos);
    CHECK(text.find("Exec=/usr/libexec/nordend/nordend-portal --file-manager1\n") !=
          std::string::npos);
    CHECK(platform::file_manager_service("/home/a b/libexec/nordend/nordend-portal")
              .find("Exec=\"/home/a b/libexec/nordend/nordend-portal\" --file-manager1") !=
          std::string::npos);

    // Set: written; who holds the name now and what opens folders are said.
    auto change =
        platform::set_file_manager(environment, "/p/nordend-portal", "thunar", "thunar.desktop");
    CHECK(change.ok);
    CHECK(read_file(service) == platform::file_manager_service("/p/nordend-portal"));
    CHECK(change.message.find("thunar holds org.freedesktop.FileManager1") != std::string::npos);
    CHECK(change.message.find("Folders open with thunar.desktop") != std::string::npos);
    change = platform::set_file_manager(environment, "/p/nordend-portal", "", "nordend.desktop");
    CHECK(change.ok);
    CHECK(change.message.find("holds") == std::string::npos);
    CHECK(change.message.find("Folders open with") == std::string::npos);

    // Unset: ours goes; another program's stays, whether setting or unsetting.
    change = platform::unset_file_manager(environment);
    CHECK(change.ok);
    CHECK_FALSE(std::filesystem::exists(service));
    CHECK(platform::unset_file_manager(environment).message ==
          "Nordend was not set to answer \"Show in Folder\".\n");
    static_cast<void>(dir.write("share/dbus-1/services/org.freedesktop.FileManager1.service",
                                "[D-BUS Service]\nName=org.freedesktop.FileManager1\n"
                                "Exec=/usr/bin/thunar --daemon\n"));
    CHECK_FALSE(platform::set_file_manager(environment, "/p/nordend-portal", "", "").ok);
    CHECK(platform::unset_file_manager(environment).message.find("another program's") !=
          std::string::npos);
    CHECK(std::filesystem::exists(service));
}

TEST_CASE("path_from_uri: local file URIs only, decoded and normalized") {
    using nordend::core::path_from_uri;
    CHECK(path_from_uri("file:///a%20b/c") == std::filesystem::path{"/a b/c"});
    CHECK(path_from_uri("FILE://localhost/x/./y/") == std::filesystem::path{"/x/y"});
    CHECK_FALSE(path_from_uri("file://other/x"));
    CHECK_FALSE(path_from_uri("http://x/y"));
    CHECK_FALSE(path_from_uri("file:///a%00b"));
}
