#include "nordend/platform/volumes.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

namespace nordend::platform {
namespace {

constexpr double kGigabyte = 1e9;
constexpr double kTerabyte = 1e12;

const UdisksObject* find(const std::vector<UdisksObject>& objects, const std::string& path) {
    const auto it = std::ranges::find(objects, path, &UdisksObject::path);
    return it == objects.end() ? nullptr : &*it;
}

}  // namespace

std::string size_name(std::uint64_t bytes) {
    const auto value = static_cast<double>(bytes);
    if (value >= kTerabyte) {
        return std::format("{:.1f} TB Volume", value / kTerabyte);
    }
    if (value >= kGigabyte) {
        return std::format("{:.0f} GB Volume", value / kGigabyte);
    }
    return std::format("{:.0f} MB Volume", std::max(1.0, std::round(value / 1e6)));
}

namespace {

// Its label; a disc image without one: its file's name; else its size, as GNOME says it.
std::string volume_name(const UdisksObject& object, bool image) {
    if (!object.id_label.empty()) {
        return object.id_label;
    }
    if (image) {
        return object.backing_file.filename().string();
    }
    return size_name(object.size);
}

}  // namespace

std::vector<Volume> volumes_from(const std::vector<UdisksObject>& objects) {
    std::vector<Volume> volumes;
    for (const UdisksObject& object : objects) {
        if (!object.block || !object.filesystem || object.hint_ignore ||
            object.id_usage != "filesystem") {
            continue;
        }
        const UdisksObject* const drive = find(objects, object.drive);
        const bool image = object.loop && !object.backing_file.empty();
        const bool external =
            image || (drive != nullptr && drive->drive_interface &&
                      (drive->removable || drive->ejectable || drive->connection_bus == "usb" ||
                       drive->connection_bus == "sdio" || drive->connection_bus == "ieee1394"));
        const bool mounted = !object.mount_points.empty();
        if (object.hint_system && !external && !mounted) {
            continue;  // another system's partition, the EFI one...
        }
        volumes.push_back(
            {.object = object.path,
             .device = object.device,
             .name = volume_name(object, image),
             .drive = object.drive,
             .external = external,
             .mount_point = mounted ? std::optional{object.mount_points.front()} : std::nullopt,
             .image = image ? object.backing_file : std::filesystem::path{},
             .uuid = object.id_uuid});
    }
    std::ranges::sort(volumes, {}, &Volume::name);
    return volumes;
}

std::vector<fs::Place> with_volumes(std::vector<fs::Place> places,
                                    const std::vector<Volume>& volumes) {
    for (fs::Place& place : places) {
        if (place.section != fs::Place::Section::devices) {
            continue;
        }
        const auto volume = std::ranges::find_if(volumes, [&place](const Volume& candidate) {
            return candidate.mount_point && *candidate.mount_point == place.path;
        });
        if (volume != volumes.end()) {
            const bool image = !volume->image.empty();
            place.disk = {.volume = volume->object,
                          .mounted = true,
                          .ejectable = volume->external,
                          .image = image,
                          .uuid = volume->uuid};
            if (image) {
                place.icon = "disc";
            } else if (volume->external) {
                place.icon = "usb";
            }
        }
    }
    for (const Volume& volume : volumes) {
        if (volume.mount_point) {
            continue;  // listed from the mount table, where it is mounted
        }
        std::string_view icon = volume.external ? "usb" : "database-off";  // literals: they last
        if (!volume.image.empty()) {
            icon = "disc";
        }
        places.push_back({.section = fs::Place::Section::devices,
                          .label = volume.name,
                          .path = {},
                          .icon = icon,
                          .disk = {.volume = volume.object,
                                   .mounted = false,
                                   .ejectable = volume.external,
                                   .image = !volume.image.empty(),
                                   .uuid = volume.uuid}});
    }
    return places;
}

std::vector<const Volume*> on_same_drive(const std::vector<Volume>& volumes,
                                         const std::string& volume) {
    const auto it = std::ranges::find(volumes, volume, &Volume::object);
    std::vector<const Volume*> found;
    if (it == volumes.end()) {
        return found;
    }
    for (const Volume& other : volumes) {
        const bool same = &other == &*it || (it->drive != "/" && other.drive == it->drive);
        if (same && other.mount_point) {
            found.push_back(&other);
        }
    }
    return found;
}

std::string power_off_refusal(std::string_view message) {
    if (const std::size_t colon = message.rfind(": "); colon != std::string_view::npos) {
        message.remove_prefix(colon + 2);
    }
    std::string reason{message};
    if (reason.empty()) {
        return "in use";
    }
    if (reason.front() >= 'A' && reason.front() <= 'Z') {
        reason.front() = static_cast<char>(reason.front() - 'A' + 'a');
    }
    return reason;
}

}  // namespace nordend::platform
