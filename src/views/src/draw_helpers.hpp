#pragma once

// Small text and chrome helpers shared by the views.

#include <span>
#include <string_view>
#include <vector>

#include "nordend/text/text.hpp"
#include "nordend/theme/palette.hpp"
#include "nordend/theme/syntax.hpp"
#include "nordend/ui/geometry.hpp"
#include "nordend/ui/renderer.hpp"

namespace nordend::views {

// Height of the bar above the views (the breadcrumb).
inline constexpr float kTitleHeight = 40.0F;

// Width of text in logical pixels.
float text_width(ui::Renderer& renderer, std::string_view text, float size);

// Text centered in bounds (with 12 px margins, ellipsized when longer), its top at y.
void draw_centered(ui::Renderer& renderer, std::string_view text, const ui::Rect& bounds, float y,
                   float size, ui::Color color);

void draw_right_aligned(ui::Renderer& renderer, std::string_view text, float right_x, float top_y,
                        float font_size, ui::Color color);

// The mark of a file that may run as a program (issue #17): a dot at the bottom right of its
// icon (the square at x, y, size wide), in dot, ringed in behind (the colour under it) so it
// stands apart from the icon on any theme. Its size follows the icon's, 6 to 12 px.
void draw_run_mark(ui::Renderer& renderer, float x, float y, float size, ui::Color dot,
                   ui::Color behind);

// A line's syntax colours as runs for Renderer::draw_text_runs, at opacity alpha.
std::vector<ui::ColorRun> colour_runs(std::span<const text::Span> spans,
                                      const theme::SyntaxColors& colours, float alpha);

}  // namespace nordend::views
