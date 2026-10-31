#include "accessibility/display_text.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

using namespace dkr::options_menu;
namespace text = dkr::accessibility::text;

namespace {

// Drawn in the game font: capitals, plain ASCII, none of the symbols it lacks.
bool menu_text(std::string_view value) {
    if (value.empty()) return false;
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20U || byte > 0x7EU) return false;
        if (c >= 'a' && c <= 'z') return false;
        if (c == '&' || c == '/' || c == '(' || c == ')') return false;
    }
    return true;
}

void every_display_item_has_a_label() {
    assert(text::display_label(ItemId::DisplayOptions) == "DISPLAY OPTIONS");
    for (int id = static_cast<int>(ItemId::Presentation); id <= static_cast<int>(ItemId::MetricResolution); ++id) {
        assert(menu_text(text::display_label(static_cast<ItemId>(id))));
    }
    assert(text::display_label(ItemId::MasterVolume).empty());
    assert(text::display_label(ItemId::Language).empty());
}

void display_pages_have_headings() {
    assert(text::display_heading(PageId::Display) == "DISPLAY OPTIONS");
    assert(text::display_heading(PageId::AdvancedGraphics) == "ADVANCED GRAPHICS");
    assert(text::display_heading(PageId::HudAndCamera) == "HUD AND CAMERA");
    assert(text::display_heading(PageId::Scenery) == "SCENERY");
    assert(text::display_heading(PageId::WaterAndLava) == "WATER AND LAVA");
    assert(text::display_heading(PageId::AdvancedWater) == "ADVANCED WATER");
    assert(text::display_heading(PageId::PerformanceOverlay) == "PERFORMANCE OVERLAY");
    assert(text::display_heading(PageId::Root).empty());
    assert(text::display_heading(PageId::Audio).empty());
}

void choice_lists_follow_the_setting_values() {
    const auto count = [](ItemId id) { return text::display_choices(id).size(); };
    assert(count(ItemId::Presentation) == 2U);
    assert(count(ItemId::WindowMode) == 2U);
    assert(count(ItemId::PerformancePreset) == 5U);
    assert(count(ItemId::Resolution) == 3U);
    assert(count(ItemId::AspectRatio) == 2U);
    assert(count(ItemId::GraphicsApi) == 4U);
    assert(count(ItemId::AntiAliasing) == 4U);
    assert(count(ItemId::Anisotropy) == 5U);
    assert(count(ItemId::ColourPrecision) == 3U);
    assert(count(ItemId::HudLayout) == 2U);
    assert(count(ItemId::CrtScaling) == 2U);
    assert(count(ItemId::SceneryCoverage) == 4U);
    assert(count(ItemId::WaterPreset) == 5U);
    assert(count(ItemId::Reflections) == 3U);
    assert(count(ItemId::ReflectionDetail) == 4U);
    assert(count(ItemId::OverlayPosition) == 4U);
    assert(count(ItemId::OverlayDetail) == 4U);
    assert(count(ItemId::OverlayLayout) == 2U);
    assert(count(ItemId::OverlayFillColour) == 8U);
    assert(count(ItemId::FieldOfView) == 0U);
    for (int id = static_cast<int>(ItemId::Presentation); id <= static_cast<int>(ItemId::MetricResolution); ++id) {
        for (const std::string_view choice : text::display_choices(static_cast<ItemId>(id))) {
            assert(menu_text(choice));
        }
    }
    assert(text::display_choices(ItemId::GraphicsApi)[1] == "DIRECT3D 12");
    assert(text::display_choices(ItemId::PerformancePreset)[1] == "BALANCED");
}

void values_are_worded_for_the_menu() {
    assert(text::display_frame_rate(std::nullopt) == "MATCH DISPLAY");
    assert(text::display_frame_rate(144) == "144 FPS");
    assert(text::display_times(3) == "3X");
    assert(text::display_supersampling(1) == "OFF");
    assert(text::display_supersampling(4) == "4X");
    assert(text::display_degrees(5) == "+5 DEGREES");
    assert(text::display_degrees(0) == "0 DEGREES");
    assert(text::display_degrees(-20) == "-20 DEGREES");
    assert(text::display_texture_bias(50) == "+0.50");
    assert(text::display_texture_bias(0) == "0.00");
    assert(text::display_texture_bias(-125) == "-1.25");
    assert(text::display_texture_bias(-200) == "-2.00");
    assert(text::display_pixels(24) == "24 PIXELS");
    assert(text::display_applies_next_game("16X") == "16X, APPLIES NEXT GAME");
    assert(text::display_preset_applied("BALANCED") == "BALANCED APPLIED");
    assert(text::display_done(ItemId::ResetWaterAppearance) == "WATER APPEARANCE RESET");
    assert(text::display_done(ItemId::ResetWaterQuality) == "WATER QUALITY RESET");
    assert(text::display_done(ItemId::WaterPreset).empty());
}

} // namespace

int main() {
    every_display_item_has_a_label();
    display_pages_have_headings();
    choice_lists_follow_the_setting_values();
    values_are_worded_for_the_menu();
    std::puts("[test][options-menu-display-text] PASS");
    return 0;
}
