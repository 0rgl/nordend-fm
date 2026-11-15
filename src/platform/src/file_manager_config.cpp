#include "nordend/platform/file_manager_config.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <fstream>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <sstream>
#include <system_error>

#include "nordend/core/file.hpp"

namespace nordend::platform {
namespace {

constexpr std::string_view kMarker = "# Written by nordend --set-file-manager";

std::string read_whole(const std::filesystem::path& path) {
    std::ifstream in{path};
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

std::string file_manager_service(const std::filesystem::path& portal) {
    std::string program = portal.string();
    if (program.find_first_of(" \t") != std::string::npos) {
        program = "\"" + program + "\"";
    }
    return std::format(
        "{}: \"Show in Folder\" opens Nordend.\n"
        "# `nordend --unset-file-manager` removes this file.\n"
        "[D-BUS Service]\n"
        "Name=org.freedesktop.FileManager1\n"
        "Exec={} --file-manager1\n",
        kMarker, program);
}

bool is_ours(std::string_view text) { return text.starts_with(kMarker); }

std::filesystem::path file_manager_service_path(const Environment& environment) {
    const std::filesystem::path home =
        environment.data_dirs.empty() ? std::filesystem::path{} : environment.data_dirs.front();
    return home / "dbus-1" / "services" / std::string{kFileManagerService};
}

std::optional<std::filesystem::path> portal_program() {
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return std::nullopt;
    }
    const std::filesystem::path folder = self.parent_path();
    const std::array<std::filesystem::path, 2> candidates{
        folder.parent_path() / "libexec" / "nordend" / "nordend-portal",  // installed
        folder / "nordend-portal",                                        // the build tree
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

ManagerChange set_file_manager(const Environment& environment, const std::filesystem::path& portal,
                               std::string_view owner, std::string_view folder_app) {
    const std::filesystem::path path = file_manager_service_path(environment);
    if (path.is_relative()) {
        return {.ok = false, .message = "no data folder to write the service file in\n"};
    }
    std::error_code error;
    if (std::filesystem::exists(path, error) && !is_ours(read_whole(path))) {
        return {.ok = false,
                .message = std::format("{} is another program's: left alone. Move it away first "
                                       "if Nordend should answer \"Show in Folder\".\n",
                                       path.string())};
    }
    if (const std::error_code failed =
            core::write_file_atomically(path, file_manager_service(portal))) {
        return {.ok = false,
                .message = std::format("cannot write {}: {}\n", path.string(), failed.message())};
    }
    std::string message =
        std::format("Wrote {}: \"Show in Folder\" now opens Nordend.\n", path.string());
    if (!owner.empty() && owner != "nordend-portal") {
        message += std::format(
            "{} holds org.freedesktop.FileManager1 right now: Nordend answers once it exits, or "
            "after you log out and in again.\n",
            owner);
    }
    if (folder_app != "nordend.desktop") {
        message += std::format(
            "Folders open with {}: `xdg-mime default nordend.desktop inode/directory` (or "
            "`nordend-setup configure`) opens them in Nordend too.\n",
            folder_app.empty() ? std::string_view{"no application"} : folder_app);
    }
    return {.ok = true, .message = std::move(message)};
}

ManagerChange unset_file_manager(const Environment& environment) {
    const std::filesystem::path path = file_manager_service_path(environment);
    std::error_code error;
    if (path.is_relative() || !std::filesystem::exists(path, error)) {
        return {.ok = true, .message = "Nordend was not set to answer \"Show in Folder\".\n"};
    }
    if (!is_ours(read_whole(path))) {
        return {.ok = true,
                .message = std::format("{} is another program's: left alone.\n", path.string())};
    }
    if (!std::filesystem::remove(path, error) || error) {
        return {.ok = false,
                .message = std::format("cannot remove {}: {}\n", path.string(), error.message())};
    }
    return {.ok = true,
            .message = std::format("Removed {}: \"Show in Folder\" goes back to the desktop's file "
                                   "manager once Nordend's service exits (30 s at most).\n",
                                   path.string())};
}

std::string bus_name_owner(std::string_view name) {
    try {
        const std::unique_ptr<sdbus::IConnection> bus = sdbus::createSessionBusConnection();
        const std::unique_ptr<sdbus::IProxy> daemon =
            sdbus::createProxy(*bus, sdbus::ServiceName{"org.freedesktop.DBus"},
                               sdbus::ObjectPath{"/org/freedesktop/DBus"});
        bool has_owner = false;
        daemon->callMethod("NameHasOwner")
            .onInterface("org.freedesktop.DBus")
            .withArguments(std::string{name})
            .storeResultsTo(has_owner);
        if (!has_owner) {
            return {};
        }
        std::uint32_t pid = 0;
        daemon->callMethod("GetConnectionUnixProcessID")
            .onInterface("org.freedesktop.DBus")
            .withArguments(std::string{name})
            .storeResultsTo(pid);
        std::string program = read_whole(std::format("/proc/{}/comm", pid));
        while (!program.empty() && (program.back() == '\n' || program.back() == '\r')) {
            program.pop_back();
        }
        return program.empty() ? std::string{"another program"} : program;
    } catch (const std::exception& /*error*/) {
        return {};
    }
}

}  // namespace nordend::platform
