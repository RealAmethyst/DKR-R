#include "display_text.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace dkr::accessibility::text {

using options_menu::ItemId;
using options_menu::PageId;
using namespace std::string_view_literals;

namespace {

constexpr std::array kPresentation{"ACCURATE"sv, "MODERN"sv};
constexpr std::array kWindowMode{"WINDOWED"sv, "FULLSCREEN"sv};
constexpr std::array kPerformancePreset{"BATTERY"sv, "BALANCED"sv, "QUALITY"sv, "LOW LOAD 30 FPS"sv,
                                        "LOW LOAD 60 FPS"sv};
constexpr std::array kResolution{"ORIGINAL 240P"sv, "ORIGINAL 2X"sv, "AUTOMATIC INTEGER SCALE"sv};
constexpr std::array kAspectRatio{"ORIGINAL 4:3"sv, "FIT TO WINDOW"sv};
constexpr std::array kGraphicsApi{"AUTOMATIC"sv, "DIRECT3D 12"sv, "VULKAN"sv, "METAL"sv};
constexpr std::array kAntiAliasing{"NONE"sv, "MSAA 2X"sv, "MSAA 4X"sv, "MSAA 8X"sv};
constexpr std::array kAnisotropy{"OFF"sv, "2X"sv, "4X"sv, "8X"sv, "16X"sv};
constexpr std::array kColourPrecision{"AUTOMATIC"sv, "ON"sv, "OFF"sv};
constexpr std::array kHudLayout{"4:3"sv, "FIT TO WINDOW"sv};
constexpr std::array kCrtScaling{"STRETCH TO VIEWPORT"sv, "TILE AT NATIVE SIZE"sv};
constexpr std::array kSceneryCoverage{"ORIGINAL"sv, "CURRENT AREA"sv, "VISIBLE AND NEIGHBOURING AREAS"sv,
                                      "FULL FORWARD VIEW"sv};
constexpr std::array kWaterPreset{"ORIGINAL"sv, "PERFORMANCE"sv, "BALANCED"sv, "HIGH"sv, "CUSTOM"sv};
constexpr std::array kReflections{"SKY"sv, "SCREEN-SPACE"sv, "HARDWARE RAY TRACING"sv};
constexpr std::array kWaterDetail{"PRESET DEFAULT"sv, "LOW"sv, "MEDIUM"sv, "HIGH"sv};
constexpr std::array kOverlayPosition{"TOP LEFT"sv, "TOP RIGHT"sv, "BOTTOM LEFT"sv, "BOTTOM RIGHT"sv};
constexpr std::array kOverlayDetail{"FPS ONLY"sv, "STANDARD"sv, "DETAILED"sv, "CUSTOM"sv};
constexpr std::array kOverlayLayout{"STACKED"sv, "SINGLE ROW"sv};
constexpr std::array kColours{"WHITE"sv, "BLACK"sv, "YELLOW"sv, "GREEN"sv, "CYAN"sv, "RED"sv, "ORANGE"sv, "PINK"sv};

std::string signed_number(int value) {
    return (value > 0 ? "+" : "") + std::to_string(value);
}

} // namespace

std::string display_label(ItemId id) {
    switch (id) {
        case ItemId::DisplayOptions: return "DISPLAY OPTIONS";
        case ItemId::Presentation: return "PRESENTATION STYLE";
        case ItemId::WindowMode: return "WINDOW MODE";
        case ItemId::PerformancePreset: return "PERFORMANCE PRESET";
        case ItemId::Resolution: return "RESOLUTION";
        case ItemId::AspectRatio: return "ASPECT RATIO";
        case ItemId::FrameRate: return "FRAME RATE";
        case ItemId::AdvancedGraphics: return "ADVANCED GRAPHICS";
        case ItemId::HudAndCamera: return "HUD AND CAMERA";
        case ItemId::Scenery: return "SCENERY";
        case ItemId::WaterAndLava: return "WATER AND LAVA";
        case ItemId::PerformanceOverlay: return "PERFORMANCE OVERLAY";
        case ItemId::GraphicsApi: return "GRAPHICS API";
        case ItemId::AntiAliasing: return "ANTI-ALIASING";
        case ItemId::Anisotropy: return "ANISOTROPIC FILTERING";
        case ItemId::ColourPrecision: return "COLOUR PRECISION";
        case ItemId::Supersampling: return "SUPERSAMPLING";
        case ItemId::TextureSharpness: return "TEXTURE SHARPNESS";
        case ItemId::TextureShimmering: return "REDUCE TEXTURE SHIMMERING";
        case ItemId::HudLayout: return "HUD LAYOUT";
        case ItemId::FieldOfView: return "FIELD OF VIEW";
        case ItemId::ModernCamera: return "MODERN CAMERA";
        case ItemId::LookBack: return "LOOK BACK";
        case ItemId::CrtFilter: return "CRT FILTER";
        case ItemId::CrtImage: return "CRT FILTER IMAGE";
        case ItemId::CrtScaling: return "CRT SCALING";
        case ItemId::CrtStrength: return "CRT FILTER STRENGTH";
        case ItemId::MaximumDetail: return "MAXIMUM VEHICLE DETAIL";
        case ItemId::KeepHubScenery: return "KEEP HUB SCENERY";
        case ItemId::KeepTrackScenery: return "KEEP TRACK AND BOSS SCENERY";
        case ItemId::KeepMinigameScenery: return "KEEP MINIGAME AND BATTLE SCENERY";
        case ItemId::ReducePopIn: return "REDUCE WIDE-SCREEN POP-IN";
        case ItemId::VisibilityMargin: return "VISIBILITY MARGIN";
        case ItemId::ViewDistance: return "SCENERY AND OBJECT DISTANCE";
        case ItemId::SceneryCoverage: return "SCENERY COVERAGE";
        case ItemId::AnimatedDistance: return "ANIMATED SCENERY DISTANCE";
        case ItemId::EffectsDistance: return "SPRITES AND EFFECTS DISTANCE";
        case ItemId::WaterLavaDistance: return "WATER AND LAVA DETAIL DISTANCE";
        case ItemId::WaterPreset: return "WATER PRESET";
        case ItemId::Reflections: return "REFLECTIONS";
        case ItemId::RacerWakes: return "RACER WAKES";
        case ItemId::WaterSpray: return "WATER SPRAY";
        case ItemId::LavaGlow: return "NEARBY LAVA GLOW";
        case ItemId::LavaIntensity: return "LAVA LIGHTING INTENSITY";
        case ItemId::LavaRange: return "LAVA LIGHT RANGE";
        case ItemId::LavaBrightness: return "LAVA BRIGHTNESS";
        case ItemId::LavaPulseStrength: return "LAVA PULSE STRENGTH";
        case ItemId::LavaPulseSpeed: return "LAVA PULSE SPEED";
        case ItemId::AdvancedWater: return "ADVANCED WATER";
        case ItemId::ReflectionDetail: return "REFLECTION DETAIL";
        case ItemId::WakeDetail: return "WAKE QUALITY";
        case ItemId::SprayDetail: return "SPRAY DETAIL";
        case ItemId::ReflectionDistance: return "HARDWARE REFLECTION DISTANCE";
        case ItemId::ReflectionStrength: return "REFLECTION STRENGTH";
        case ItemId::SurfaceRoughness: return "SURFACE ROUGHNESS";
        case ItemId::RippleStrength: return "FINE RIPPLE STRENGTH";
        case ItemId::FoamIntensity: return "FOAM INTENSITY";
        case ItemId::WakeVisibility: return "WAKE VISIBILITY";
        case ItemId::WaterClarity: return "WATER CLARITY";
        case ItemId::RefractionStrength: return "REFRACTION STRENGTH";
        case ItemId::WaterfallDetail: return "WATERFALL FLOWING DETAIL";
        case ItemId::TrackColours: return "USE TRACK COLOURS";
        case ItemId::TintRed: return "WATER TINT RED";
        case ItemId::TintGreen: return "WATER TINT GREEN";
        case ItemId::TintBlue: return "WATER TINT BLUE";
        case ItemId::ResetWaterAppearance: return "RESET WATER APPEARANCE";
        case ItemId::ResetWaterQuality: return "RESET WATER QUALITY";
        case ItemId::RecordWaterPerformance: return "RECORD WATER PERFORMANCE";
        case ItemId::OverlayShow: return "SHOW PERFORMANCE OVERLAY";
        case ItemId::OverlayPosition: return "OVERLAY POSITION";
        case ItemId::OverlayDetail: return "DETAIL PRESET";
        case ItemId::OverlayLayout: return "METRIC LAYOUT";
        case ItemId::OverlayFontSize: return "FONT SIZE";
        case ItemId::OverlayFillColour: return "FONT FILL COLOUR";
        case ItemId::OverlayOutlineColour: return "FONT OUTLINE COLOUR";
        case ItemId::MetricFrameTime: return "FRAME TIME";
        case ItemId::MetricSimulation: return "SIMULATION RATE";
        case ItemId::MetricGraphics: return "GRAPHICS TASK RATE";
        case ItemId::MetricVi: return "VI RATE";
        case ItemId::MetricInterpolation: return "INTERPOLATED FRAME RATE";
        case ItemId::MetricAudio: return "AUDIO SAMPLE RATE";
        case ItemId::MetricTarget: return "PRESENTATION TARGET";
        case ItemId::MetricResolution: return "VIEWPORT RESOLUTION";
        default: return {};
    }
}

std::string display_heading(PageId page) {
    switch (page) {
        case PageId::Display: return "DISPLAY OPTIONS";
        case PageId::AdvancedGraphics: return "ADVANCED GRAPHICS";
        case PageId::HudAndCamera: return "HUD AND CAMERA";
        case PageId::Scenery: return "SCENERY";
        case PageId::WaterAndLava: return "WATER AND LAVA";
        case PageId::AdvancedWater: return "ADVANCED WATER";
        case PageId::PerformanceOverlay: return "PERFORMANCE OVERLAY";
        default: return {};
    }
}

std::span<const std::string_view> display_choices(ItemId id) {
    switch (id) {
        case ItemId::Presentation: return kPresentation;
        case ItemId::WindowMode: return kWindowMode;
        case ItemId::PerformancePreset: return kPerformancePreset;
        case ItemId::Resolution: return kResolution;
        case ItemId::AspectRatio: return kAspectRatio;
        case ItemId::GraphicsApi: return kGraphicsApi;
        case ItemId::AntiAliasing: return kAntiAliasing;
        case ItemId::Anisotropy: return kAnisotropy;
        case ItemId::ColourPrecision: return kColourPrecision;
        case ItemId::HudLayout: return kHudLayout;
        case ItemId::CrtScaling: return kCrtScaling;
        case ItemId::SceneryCoverage: return kSceneryCoverage;
        case ItemId::WaterPreset: return kWaterPreset;
        case ItemId::Reflections: return kReflections;
        case ItemId::ReflectionDetail:
        case ItemId::WakeDetail:
        case ItemId::SprayDetail:
            return kWaterDetail;
        case ItemId::OverlayPosition: return kOverlayPosition;
        case ItemId::OverlayDetail: return kOverlayDetail;
        case ItemId::OverlayLayout: return kOverlayLayout;
        case ItemId::OverlayFillColour:
        case ItemId::OverlayOutlineColour:
            return kColours;
        default: return {};
    }
}

std::string display_frame_rate(std::optional<int> target) {
    return target ? std::to_string(*target) + " FPS" : std::string("MATCH DISPLAY");
}

std::string display_times(int value) {
    return std::to_string(value) + "X";
}

std::string display_supersampling(int value) {
    return value <= 1 ? std::string("OFF") : display_times(value);
}

std::string display_degrees(int value) {
    return signed_number(value) + " DEGREES";
}

std::string display_texture_bias(int hundredths) {
    char number[16]{};
    const int whole = std::abs(hundredths);
    std::snprintf(number, sizeof(number), "%s%d.%02d", hundredths > 0 ? "+" : hundredths < 0 ? "-" : "",
                  whole / 100, whole % 100);
    return number;
}

std::string display_pixels(int value) {
    return std::to_string(value) + " PIXELS";
}

std::string display_applies_next_game(std::string_view value) {
    return std::string(value) + ", APPLIES NEXT GAME";
}

std::string display_preset_applied(std::string_view preset) {
    return std::string(preset) + " APPLIED";
}

std::string display_done(ItemId id) {
    switch (id) {
        case ItemId::ResetWaterAppearance: return "WATER APPEARANCE RESET";
        case ItemId::ResetWaterQuality: return "WATER QUALITY RESET";
        default: return {};
    }
}

} // namespace dkr::accessibility::text
