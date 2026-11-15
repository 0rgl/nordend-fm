#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "nordend/platform/desktop_entry.hpp"

// `nordend --set-file-manager` / `--unset-file-manager` (issue #16): Nordend as the program
// "Show in Folder" reaches, by a D-Bus service file for org.freedesktop.FileManager1 in the
// user's own dbus-1/services, which the session bus reads before the system's (where Thunar,
// Nautilus or Dolphin put theirs). Never installed for everyone: two system files for one name
// would be a conflict.
namespace nordend::platform {

inline constexpr std::string_view kFileManagerBusName = "org.freedesktop.FileManager1";
inline constexpr std::string_view kFileManagerService = "org.freedesktop.FileManager1.service";

// The service file's text: the name, and nordend-portal --file-manager1 at portal (quoted when
// the path has a space), with a first line saying who wrote it.
[[nodiscard]] std::string file_manager_service(const std::filesystem::path& portal);

// Whether text is a service file set_file_manager() wrote.
[[nodiscard]] bool is_ours(std::string_view text);

// Where the user's service file goes: the first data dir (XDG_DATA_HOME)'s dbus-1/services.
[[nodiscard]] std::filesystem::path file_manager_service_path(const Environment& environment);

// nordend-portal beside this program: <prefix>/libexec/nordend/nordend-portal for an installed
// <prefix>/bin/nordend, else in the same folder (the build tree). nullopt when it is not there.
[[nodiscard]] std::optional<std::filesystem::path> portal_program();

struct ManagerChange {
    bool ok{true};        // false: the file could not be written or removed
    std::string message;  // what was done, lines for the terminal
};

// Writes the service file naming portal; then says who holds the bus name now (owner: the
// program's name, empty when nobody does) and whether folders open in Nordend (folder_app: the
// desktop file ID that opens inode/directory, empty when none).
[[nodiscard]] ManagerChange set_file_manager(const Environment& environment,
                                             const std::filesystem::path& portal,
                                             std::string_view owner, std::string_view folder_app);

// Removes the service file when it is ours; another one is left alone, and said.
[[nodiscard]] ManagerChange unset_file_manager(const Environment& environment);

// The program holding bus name on the session bus right now ("thunar"), from its process; empty
// when nobody does or the bus cannot be asked.
[[nodiscard]] std::string bus_name_owner(std::string_view name);

}  // namespace nordend::platform
