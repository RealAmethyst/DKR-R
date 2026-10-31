#pragma once

#include "options_menu/options_menu.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Mod-owned wording for DISPLAY OPTIONS and its sub-pages, part of the
// central localization (localization.hpp holds the rest). Capitals, like the
// game's menu text; it is drawn in the game font and spoken as written.
namespace dkr::accessibility::text {

// Row labels for the display items, including the rows that open sub-pages
// and the DISPLAY OPTIONS row of the top page.
std::string display_label(options_menu::ItemId id);

// Headings of DISPLAY OPTIONS and its sub-pages.
std::string display_heading(options_menu::PageId page);

// The choices of a display row that picks from a list, in the order of the
// setting's own values; empty for rows that are not lists. Indices:
// - PerformancePreset: mobile_graphics::Preset (CUSTOM is phrase(Custom)).
// - GraphicsApi: ultramodern GraphicsApi (Automatic, Direct3D 12, Vulkan, Metal).
// - Anisotropy: the levels 1x, 2x, 4x, 8x, 16x.
// - ReflectionDetail, WakeDetail, SprayDetail: the stored value plus one.
// - OverlayFillColour, OverlayOutlineColour: the bridge's named colours.
std::span<const std::string_view> display_choices(options_menu::ItemId id);

// FRAME RATE: MATCH DISPLAY when no manual target is set, else "60 FPS".
std::string display_frame_rate(std::optional<int> target);
// A distance multiplier: "2X".
std::string display_times(int value);
// Supersampling: OFF at 1, else "2X".
std::string display_supersampling(int value);
// A field-of-view offset: "+5 DEGREES", "0 DEGREES", "-5 DEGREES".
std::string display_degrees(int value);
// Texture sharpness from hundredths: "+0.50", "0.00", "-1.25".
std::string display_texture_bias(int hundredths);
// The overlay font size: "24 PIXELS".
std::string display_pixels(int value);
// A saved choice the running game has not picked up yet: "16X, APPLIES NEXT GAME".
std::string display_applies_next_game(std::string_view value);
// What A on PERFORMANCE PRESET says once it is applied: "BALANCED APPLIED".
std::string display_preset_applied(std::string_view preset);
// What A on a reset row says once it is done; empty for other items.
std::string display_done(options_menu::ItemId id);

} // namespace dkr::accessibility::text
