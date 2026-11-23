#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nordend/theme/palette.hpp"
#include "nordend/ui/geometry.hpp"
#include "nordend/ui/icons.hpp"
#include "nordend/ui/renderer.hpp"
#include "nordend/ui/seat.hpp"
#include "nordend/views/folder.hpp"

namespace nordend::views {

// One step of a path in the breadcrumb: the home or the root as an icon, then a folder's name.
struct Crumb {
    std::filesystem::path path;
    std::string label;      // empty for the first crumb, which is an icon
    std::string_view icon;  // set for the first crumb only

    bool operator==(const Crumb&) const = default;
};

// folder's steps: from the home (an icon) when folder is in it, else from the root.
[[nodiscard]] std::vector<Crumb> crumbs_of(const std::filesystem::path& folder,
                                           const std::filesystem::path& home);

// The bar above the view: the folder's path as clickable steps, cut from the left with "…" when
// it does not fit (the first step and the last ones stay). A click on a step asks to go there,
// with the cursor on the step after it, the folder we came from; a click on the last step, the
// folder shown, asks for the path field. Inside an archive (0.9.0) its step has an archive's
// icon, and a quiet "read-only" follows the last step.
//
// Loop thread only.
class Breadcrumb {
public:
    // go(path, child): the step at path was clicked; child is the name of the step after it
    // (empty for the last).
    using Go = std::function<void(const std::filesystem::path& path, const std::string& child)>;

    // edit(): the last step, the folder shown, was clicked.
    Breadcrumb(std::function<void()> redraw, Go go, std::function<void()> edit,
               std::filesystem::path home);

    void draw(ui::Renderer& renderer, const ui::Rect& bounds, const theme::Palette& palette,
              ui::IconSet& icons, const Folder& folder);

    // The pointer at (x, y) is over the bar as last drawn.
    [[nodiscard]] bool claims(float x, float y) const;

    // A crumb that opens its folder is there (not the last: the folder shown).
    [[nodiscard]] bool clickable_at(float x, float y) const;

    // The last step, the folder shown, is there: a text cursor, and a click opens the path field.
    [[nodiscard]] bool editable_at(float x, float y) const;

    void pointer(const ui::PointerEvent& event);

    // The folder of the step at a window point that files can be dropped into, as last drawn
    // (nullopt: none, the "…", or a step inside an archive).
    [[nodiscard]] std::optional<std::filesystem::path> path_at(float x, float y) const;

    // A drag over the window would drop into the step at this path (nullopt: none), drawn
    // marked.
    void set_drop_target(std::optional<std::filesystem::path> path);

    // The steps shown in the last draw: the first one's icon name, then the folders' names, "…"
    // where steps were left out (tests).
    [[nodiscard]] std::vector<std::string> shown() const;

    // The archive whose step was drawn with its icon, and "read-only" after the last step
    // (empty: not inside an archive) (tests).
    [[nodiscard]] const std::filesystem::path& archive() const noexcept { return archive_; }

private:
    struct Shown {
        std::optional<std::size_t> crumb;  // nullopt: the "…"
        ui::Rect rect{};
    };

    [[nodiscard]] std::optional<std::size_t> crumb_at(float x, float y) const;
    // The step is the archive's, or inside it.
    [[nodiscard]] bool in_archive(const Crumb& crumb) const;

    std::function<void()> redraw_;
    Go go_;
    std::function<void()> edit_;
    std::filesystem::path home_;
    std::vector<Crumb> crumbs_;      // as last drawn
    std::filesystem::path archive_;  // the folder shown is inside it (empty: not in one)
    std::vector<Shown> shown_;
    ui::Rect bounds_{};
    std::optional<std::size_t> hovered_;  // a crumb
    std::optional<std::filesystem::path> drop_target_;
};

}  // namespace nordend::views
