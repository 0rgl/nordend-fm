#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include <string_view>
#include <vector>

#include "nordend/core/event_loop.hpp"
#include "nordend/platform/bus_loop.hpp"

// org.freedesktop.FileManager1 (issue #16): what browsers and other applications call for "Show
// in Folder". nordend-portal --file-manager1 serves it once `nordend --set-file-manager` has put
// its service file in the user's dbus-1/services; each call starts one `nordend`.
// https://www.freedesktop.org/wiki/Specifications/file-manager-interface/
namespace nordend::portal {

inline constexpr std::string_view kFileManagerName = "org.freedesktop.FileManager1";
inline constexpr std::string_view kFileManagerPath = "/org/freedesktop/FileManager1";
inline constexpr std::string_view kFileManagerInterface = "org.freedesktop.FileManager1";

// The three methods, as nordend shows them.
enum class Show : std::uint8_t {
    folders,     // ShowFolders: each folder opened
    items,       // ShowItems: each item's folder, the item selected
    properties,  // ShowItemProperties: the same, the info pane shown with "More details"
};

// nordend's arguments for a call: "--reveal" or "--properties" (none for folders), "--", then
// the local paths of uris in their order (file:// only; others left out). Empty when no URI
// names a local file: nothing to start.
[[nodiscard]] std::vector<std::string> show_arguments(Show show,
                                                      const std::vector<std::string>& uris);

// org.freedesktop.FileManager1 on connection at kFileManagerPath: each call answers at once
// (Firefox gives up after a second, and opens the folder itself then) and starts program with
// show_arguments, the caller's startup_id as XDG_ACTIVATION_TOKEN (and DESKTOP_STARTUP_ID) when
// it gave one. Loop thread only; connection and bus outlive it.
class FileManager {
public:
    FileManager(core::EventLoop& loop, sdbus::IConnection& connection, platform::BusLoop& bus,
                std::filesystem::path program);
    ~FileManager();

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    FileManager(FileManager&&) = delete;
    FileManager& operator=(FileManager&&) = delete;

    // Called once nothing has been asked for `after` (from now, or since the last call): the
    // service closes then.
    void set_idle_handler(std::chrono::milliseconds after, std::function<void()> idle);

    // How many nordend processes it has started (tests).
    [[nodiscard]] std::size_t started() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nordend::portal
