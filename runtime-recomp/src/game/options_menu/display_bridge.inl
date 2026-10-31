// Included into runtime_ui.cpp after runtime_bridge.inl: DISPLAY OPTIONS and
// its sub-pages in the redesigned in-game Options menu (display_bridge.hpp).
// Each change runs the same setters, the same RememberModernGraphics /
// set_graphics_config / SaveSettings sequence and the same side effects as
// the launcher's Graphics page (runtime_graphics_ui.inl) and CRT section
// (runtime_details_ui.inl), so both surfaces stay in step. Rows the launcher
// hides or disables are hidden here.

#include "accessibility/display_text.hpp"
#include "accessibility/localization.hpp"
#include "options_menu/display_bridge.hpp"

namespace {

using MenuGraphicsItem = dkr::options_menu::ItemId;

constexpr int kMenuGraphicsPercentStep = 5;
constexpr int kMenuGraphicsFontStep = 4;
constexpr int kMenuGraphicsBiasStep = 25; // hundredths: 0.25 per press

// Manual frame-rate targets after MATCH DISPLAY.
constexpr std::array<int, 9> kMenuGraphicsFrameRates{30, 60, 75, 90, 120, 144, 165, 240, 360};
// The anisotropic filtering levels, in the order of the launcher's dropdown.
constexpr std::array<int, 5> kMenuGraphicsAnisotropy{1, 2, 4, 8, 16};

// The graphics APIs the launcher offers on this platform.
#if defined(_WIN32)
constexpr std::array kMenuGraphicsApis{GraphicsApi::Auto, GraphicsApi::D3D12, GraphicsApi::Vulkan};
#elif defined(__linux__)
constexpr std::array kMenuGraphicsApis{GraphicsApi::Auto, GraphicsApi::Vulkan};
#elif defined(__APPLE__)
constexpr std::array kMenuGraphicsApis{GraphicsApi::Metal}; // fixed: Metal (automatic)
#else
constexpr std::array kMenuGraphicsApis{GraphicsApi::Auto}; // fixed
#endif

// Performance presets the launcher offers; the low-load ones are Android only.
#if defined(__ANDROID__)
constexpr bool kMenuGraphicsLowLoad = true;
#else
constexpr bool kMenuGraphicsLowLoad = false;
#endif
constexpr int kMenuGraphicsPresetCount = kMenuGraphicsLowLoad ? 5 : 3;
constexpr int kMenuGraphicsCustomPreset = kMenuGraphicsPresetCount;
// The pending pick follows the saved settings, but is kept until applied,
// as the launcher keeps its own.
int g_menu_graphics_preset = kMenuGraphicsCustomPreset;
int g_menu_graphics_preset_synced = -1;

// The performance overlay's named colours, in the order of
// display_choices(OverlayFillColour). RED and ORANGE are the launcher's
// default fill and outline.
struct MenuGraphicsColour {
    int r, g, b;
};
constexpr std::array<MenuGraphicsColour, 8> kMenuGraphicsColours{{
    {255, 255, 255}, // WHITE
    {0, 0, 0},       // BLACK
    {255, 255, 0},   // YELLOW
    {0, 255, 0},     // GREEN
    {0, 255, 255},   // CYAN
    {245, 46, 26},   // RED
    {255, 148, 10},  // ORANGE
    {255, 105, 180}, // PINK
}};

bool MenuGraphicsModern() {
    return dkr::runtime::enhancements::modern_options_visible(dkr::runtime::enhancements::presentation_profile());
}

bool MenuGraphicsOwnedCamera() {
    return dkr::runtime::netplay::experimental::owned_runtime_active();
}

int MenuGraphicsWrap(int index, int step, int count) {
    return ((index + step) % count + count) % count;
}

std::string MenuGraphicsToggle(bool on) {
    namespace text = dkr::accessibility::text;
    return text::phrase(on ? text::Phrase::On : text::Phrase::Off);
}

std::string MenuGraphicsChoice(MenuGraphicsItem id, int index) {
    const auto choices = dkr::accessibility::text::display_choices(id);
    if (index < 0 || static_cast<std::size_t>(index) >= choices.size()) return {};
    return std::string(choices[static_cast<std::size_t>(index)]);
}

// The launcher's Game display card writes these renderer fields back every
// frame and commits them when a setting changed: refresh from the Modern
// globals (Original 30 in Accurate), Accurate's enforced values, the clamped
// supersampling and the original HUD ratio. Then it remembers the Modern
// choices, applies the configuration and saves.
void MenuGraphicsCommit(GraphicsConfig& config) {
    namespace enh = dkr::runtime::enhancements;
    if (enh::modern_presentation_enabled()) {
        config.rr_option = g_modern_refresh_mode;
        config.rr_manual_value = g_modern_refresh_target;
    } else {
        config.rr_option = RefreshRate::Original;
        config.rr_manual_value = 30;
    }
    const bool modern = MenuGraphicsModern();
    if (modern) {
        config.hr_option = HUDRatioMode::Original;
        config.ds_option = std::clamp(config.ds_option, 1, 4);
        RememberModernGraphics(config);
    } else {
        ApplyProfileGraphics(config, enh::PresentationProfile::Accurate);
    }
    ultramodern::renderer::set_graphics_config(config);
    SaveSettings();
}

// Steps a list index and stores it in the renderer configuration.
template <typename Field>
bool MenuGraphicsStepConfig(Field GraphicsConfig::*field, int count, int step) {
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    const int current = std::clamp(static_cast<int>(config.*field), 0, count - 1);
    config.*field = static_cast<Field>(MenuGraphicsWrap(current, step, count));
    MenuGraphicsCommit(config);
    return true;
}

// The preset the saved settings match, or CUSTOM (launcher: matching_preset
// in Modern only).
int MenuGraphicsAppliedPreset() {
    namespace enh = dkr::runtime::enhancements;
    if (!enh::modern_presentation_enabled()) return kMenuGraphicsCustomPreset;
    const auto applied = dkr::runtime::mobile_graphics::matching_preset(ultramodern::renderer::get_graphics_config(),
                                                                         enh::anisotropy_level(), kMenuGraphicsLowLoad);
    return applied ? static_cast<int>(*applied) : kMenuGraphicsCustomPreset;
}

int MenuGraphicsPendingPreset() {
    const int applied = MenuGraphicsAppliedPreset();
    if (applied != g_menu_graphics_preset_synced) {
        g_menu_graphics_preset = applied;
        g_menu_graphics_preset_synced = applied;
    }
    return g_menu_graphics_preset;
}

std::string MenuGraphicsPresetName(int preset) {
    namespace text = dkr::accessibility::text;
    return preset >= kMenuGraphicsCustomPreset ? text::phrase(text::Phrase::Custom)
                                               : MenuGraphicsChoice(MenuGraphicsItem::PerformancePreset, preset);
}

// FRAME RATE positions: 0 is MATCH DISPLAY, then kMenuGraphicsFrameRates.
int MenuGraphicsNextFrameRate(int step) {
    constexpr int count = static_cast<int>(kMenuGraphicsFrameRates.size()) + 1;
    if (g_modern_refresh_mode != RefreshRate::Manual) return MenuGraphicsWrap(0, step, count);
    const int target = g_modern_refresh_target;
    const auto found = std::find(kMenuGraphicsFrameRates.begin(), kMenuGraphicsFrameRates.end(), target);
    if (found != kMenuGraphicsFrameRates.end()) {
        return MenuGraphicsWrap(static_cast<int>(found - kMenuGraphicsFrameRates.begin()) + 1, step, count);
    }
    // A target set elsewhere (the launcher's 30-500 slider): the nearest
    // listed target in the direction pressed, wrapping past either end.
    if (step > 0) {
        for (std::size_t index = 0; index < kMenuGraphicsFrameRates.size(); ++index) {
            if (kMenuGraphicsFrameRates[index] > target) return static_cast<int>(index) + 1;
        }
        return 0;
    }
    for (std::size_t index = kMenuGraphicsFrameRates.size(); index-- > 0;) {
        if (kMenuGraphicsFrameRates[index] < target) return static_cast<int>(index) + 1;
    }
    return 0;
}

int MenuGraphicsAnisotropyIndex() {
    // As the launcher: the matching level, else the first.
    int found = 0;
    for (std::size_t index = 0; index < kMenuGraphicsAnisotropy.size(); ++index) {
        if (kMenuGraphicsAnisotropy[index] == dkr::runtime::enhancements::anisotropy_level()) {
            found = static_cast<int>(index);
        }
    }
    return found;
}

int MenuGraphicsApiIndex() {
    const GraphicsApi api = ultramodern::renderer::get_graphics_config().api_option;
    const auto found = std::find(kMenuGraphicsApis.begin(), kMenuGraphicsApis.end(), api);
    return found == kMenuGraphicsApis.end() ? 0 : static_cast<int>(found - kMenuGraphicsApis.begin());
}

std::string MenuGraphicsUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c < 0x80U ? static_cast<char>(std::toupper(c)) : static_cast<char>(c);
    });
    return value;
}

int MenuGraphicsCrtDensity() {
    return static_cast<int>(std::round(std::clamp(g_crt_strength, 0.0F, 1.0F) * 100.0F));
}

// The named colour an overlay colour matches. The settings file stores each
// channel truncated to 0-255, so a saved colour may be one off.
std::optional<int> MenuGraphicsColourIndex(const ImVec4& colour) {
    const auto channel = [](float value) { return static_cast<int>(std::lround(value * 255.0F)); };
    if (channel(colour.w) < 254) return std::nullopt;
    for (std::size_t index = 0; index < kMenuGraphicsColours.size(); ++index) {
        const MenuGraphicsColour& named = kMenuGraphicsColours[index];
        if (std::abs(channel(colour.x) - named.r) <= 1 && std::abs(channel(colour.y) - named.g) <= 1 &&
            std::abs(channel(colour.z) - named.b) <= 1) {
            return static_cast<int>(index);
        }
    }
    return std::nullopt;
}

ImVec4* MenuGraphicsColourSetting(MenuGraphicsItem id) {
    switch (id) {
        case MenuGraphicsItem::OverlayFillColour: return &g_fps_fill_colour;
        case MenuGraphicsItem::OverlayOutlineColour: return &g_fps_outline_colour;
        default: return nullptr;
    }
}

bool* MenuGraphicsMetric(MenuGraphicsItem id) {
    switch (id) {
        case MenuGraphicsItem::MetricFrameTime: return &g_fps_custom_frame_time;
        case MenuGraphicsItem::MetricSimulation: return &g_fps_custom_simulation;
        case MenuGraphicsItem::MetricGraphics: return &g_fps_custom_graphics;
        case MenuGraphicsItem::MetricVi: return &g_fps_custom_vi;
        case MenuGraphicsItem::MetricInterpolation: return &g_fps_custom_interpolation;
        case MenuGraphicsItem::MetricAudio: return &g_fps_custom_audio;
        case MenuGraphicsItem::MetricTarget: return &g_fps_custom_target;
        case MenuGraphicsItem::MetricResolution: return &g_fps_custom_resolution;
        default: return nullptr;
    }
}

// Scenery toggles: what the launcher reads, and its setter.
struct MenuGraphicsSceneryToggle {
    bool (*requested)();
    void (*set)(bool);
};

std::optional<MenuGraphicsSceneryToggle> MenuGraphicsScenery(MenuGraphicsItem id) {
    namespace enh = dkr::runtime::enhancements;
    switch (id) {
        case MenuGraphicsItem::MaximumDetail:
            return MenuGraphicsSceneryToggle{enh::maximum_detail_requested, enh::set_maximum_detail_enabled};
        case MenuGraphicsItem::KeepHubScenery:
            return MenuGraphicsSceneryToggle{enh::keep_hub_scenery_requested, enh::set_keep_hub_scenery_enabled};
        case MenuGraphicsItem::KeepTrackScenery:
            return MenuGraphicsSceneryToggle{enh::keep_track_scenery_requested, enh::set_keep_track_scenery_enabled};
        case MenuGraphicsItem::KeepMinigameScenery:
            return MenuGraphicsSceneryToggle{enh::keep_minigame_scenery_requested,
                                             enh::set_keep_minigame_scenery_enabled};
        case MenuGraphicsItem::ReducePopIn:
            return MenuGraphicsSceneryToggle{enh::extended_culling_requested, enh::set_extended_culling_enabled};
        default: return std::nullopt;
    }
}

// View distance multipliers: getter, upper limit and setter.
struct MenuGraphicsDistance {
    int (*get)();
    int maximum;
    void (*set)(int);
};

std::optional<MenuGraphicsDistance> MenuGraphicsDistanceOf(MenuGraphicsItem id) {
    namespace enh = dkr::runtime::enhancements;
    switch (id) {
        case MenuGraphicsItem::ViewDistance:
            return MenuGraphicsDistance{enh::view_distance_multiplier, enh::kMaximumViewDistanceMultiplier,
                                        enh::set_view_distance_multiplier};
        case MenuGraphicsItem::AnimatedDistance:
            return MenuGraphicsDistance{enh::animated_scenery_distance_multiplier,
                                        enh::kMaximumAnimatedSceneryMultiplier,
                                        enh::set_animated_scenery_distance_multiplier};
        case MenuGraphicsItem::EffectsDistance:
            return MenuGraphicsDistance{enh::billboard_effect_distance_multiplier,
                                        enh::kMaximumBillboardEffectMultiplier,
                                        enh::set_billboard_effect_distance_multiplier};
        case MenuGraphicsItem::WaterLavaDistance:
            return MenuGraphicsDistance{enh::water_lava_detail_multiplier, enh::kMaximumWaterLavaDetailMultiplier,
                                        enh::set_water_lava_detail_multiplier};
        default: return std::nullopt;
    }
}

#if defined(DKR_ENHANCED_WATER)
// The water card's percentage sliders: setting and the launcher's range.
struct MenuGraphicsWaterPercent {
    std::atomic<int>* setting;
    int low, high;
};

std::optional<MenuGraphicsWaterPercent> MenuGraphicsWaterPercentOf(MenuGraphicsItem id) {
    namespace water = dkr::runtime::water::enhanced;
    switch (id) {
        case MenuGraphicsItem::LavaIntensity: return MenuGraphicsWaterPercent{&water::lava_lighting, 0, 300};
        case MenuGraphicsItem::LavaRange: return MenuGraphicsWaterPercent{&water::lava_light_range, 25, 200};
        case MenuGraphicsItem::LavaBrightness:
            return MenuGraphicsWaterPercent{&water::lava_surface_brightness, 25, 200};
        case MenuGraphicsItem::LavaPulseStrength:
            return MenuGraphicsWaterPercent{&water::lava_pulse_strength, 0, 100};
        case MenuGraphicsItem::LavaPulseSpeed: return MenuGraphicsWaterPercent{&water::lava_pulse_speed, 25, 200};
        case MenuGraphicsItem::ReflectionDistance:
            return MenuGraphicsWaterPercent{&water::reflection_distance, 25, 200};
        case MenuGraphicsItem::ReflectionStrength:
            return MenuGraphicsWaterPercent{&water::reflection_strength, 0, 200};
        case MenuGraphicsItem::SurfaceRoughness: return MenuGraphicsWaterPercent{&water::roughness_scale, 25, 200};
        case MenuGraphicsItem::RippleStrength: return MenuGraphicsWaterPercent{&water::ripple_scale, 0, 200};
        case MenuGraphicsItem::FoamIntensity: return MenuGraphicsWaterPercent{&water::foam_scale, 0, 200};
        case MenuGraphicsItem::WakeVisibility: return MenuGraphicsWaterPercent{&water::wake_strength, 0, 200};
        case MenuGraphicsItem::WaterClarity: return MenuGraphicsWaterPercent{&water::clarity_scale, 25, 200};
        case MenuGraphicsItem::RefractionStrength:
            return MenuGraphicsWaterPercent{&water::refraction_scale, 0, 200};
        case MenuGraphicsItem::WaterfallDetail: return MenuGraphicsWaterPercent{&water::waterfall_detail, 0, 200};
        case MenuGraphicsItem::TintRed: return MenuGraphicsWaterPercent{&water::tint_red, 0, 200};
        case MenuGraphicsItem::TintGreen: return MenuGraphicsWaterPercent{&water::tint_green, 0, 200};
        case MenuGraphicsItem::TintBlue: return MenuGraphicsWaterPercent{&water::tint_blue, 0, 200};
        default: return std::nullopt;
    }
}

std::atomic<int>* MenuGraphicsWaterDetail(MenuGraphicsItem id) {
    namespace water = dkr::runtime::water::enhanced;
    switch (id) {
        case MenuGraphicsItem::ReflectionDetail: return &water::reflection_detail;
        case MenuGraphicsItem::WakeDetail: return &water::wake_detail;
        case MenuGraphicsItem::SprayDetail: return &water::spray_detail;
        default: return nullptr;
    }
}

std::atomic<bool>* MenuGraphicsWaterToggle(MenuGraphicsItem id) {
    namespace water = dkr::runtime::water::enhanced;
    switch (id) {
        case MenuGraphicsItem::RacerWakes: return &water::wakes_enabled;
        case MenuGraphicsItem::WaterSpray: return &water::spray_enabled;
        case MenuGraphicsItem::TrackColours: return &water::use_track_colours;
        case MenuGraphicsItem::RecordWaterPerformance: return &water::timing_enabled;
        default: return nullptr;
    }
}
#endif

// The water card is usable in Modern only; everything below its preset is
// disabled while the preset is Original.
bool MenuGraphicsWaterCard() {
#if defined(DKR_ENHANCED_WATER)
    return MenuGraphicsModern();
#else
    return false;
#endif
}

bool MenuGraphicsWaterTuning() {
#if defined(DKR_ENHANCED_WATER)
    return MenuGraphicsWaterCard() &&
           dkr::runtime::water::enhanced::requested_quality.load() != dkr::runtime::water::enhanced::Quality::Original;
#else
    return false;
#endif
}

} // namespace

bool dkr::runtime::ui::display::handles(options_menu::ItemId id) {
    // The Display through Performance overlay sections of ItemId.
    const auto value = static_cast<int>(id);
    return value >= static_cast<int>(options_menu::ItemId::Presentation) &&
           value <= static_cast<int>(options_menu::ItemId::MetricResolution);
}

bool dkr::runtime::ui::display::visible(options_menu::ItemId id) {
    using Item = options_menu::ItemId;
    namespace enh = dkr::runtime::enhancements;
    const bool modern = MenuGraphicsModern();
    switch (id) {
        case Item::Presentation:
        case Item::WindowMode:
        case Item::PerformancePreset:
            return true;
        case Item::FieldOfView:
        case Item::ModernCamera:
        case Item::LookBack:
            // Disabled while online play owns the camera.
            return modern && !MenuGraphicsOwnedCamera();
        case Item::CrtImage:
            return modern && g_crt_enabled && !g_crt_filters.empty();
        case Item::CrtScaling:
        case Item::CrtStrength:
            return modern && g_crt_enabled;
        case Item::MaximumDetail:
#if defined(DKR_LOCAL_MODEL_DETAIL)
            return modern;
#else
            return modern && !MenuGraphicsOwnedCamera();
#endif
        case Item::VisibilityMargin:
            return modern && enh::extended_culling_requested();
        case Item::WaterLavaDistance:
            return modern && !MenuGraphicsOwnedCamera();
        case Item::WaterAndLava:
        case Item::WaterPreset:
            return MenuGraphicsWaterCard();
        case Item::LavaIntensity:
        case Item::LavaRange:
#if defined(DKR_ENHANCED_WATER)
            return MenuGraphicsWaterTuning() && dkr::runtime::water::enhanced::lava_lighting_enabled.load() != 0;
#else
            return false;
#endif
        case Item::TintRed:
        case Item::TintGreen:
        case Item::TintBlue:
#if defined(DKR_ENHANCED_WATER)
            return MenuGraphicsWaterTuning() && !dkr::runtime::water::enhanced::use_track_colours.load();
#else
            return false;
#endif
        case Item::Reflections:
        case Item::RacerWakes:
        case Item::WaterSpray:
        case Item::LavaGlow:
        case Item::LavaBrightness:
        case Item::LavaPulseStrength:
        case Item::LavaPulseSpeed:
        case Item::AdvancedWater:
        case Item::ReflectionDetail:
        case Item::WakeDetail:
        case Item::SprayDetail:
        case Item::ReflectionDistance:
        case Item::ReflectionStrength:
        case Item::SurfaceRoughness:
        case Item::RippleStrength:
        case Item::FoamIntensity:
        case Item::WakeVisibility:
        case Item::WaterClarity:
        case Item::RefractionStrength:
        case Item::WaterfallDetail:
        case Item::TrackColours:
        case Item::ResetWaterAppearance:
        case Item::ResetWaterQuality:
        case Item::RecordWaterPerformance:
            return MenuGraphicsWaterTuning();
        case Item::OverlayPosition:
        case Item::OverlayDetail:
        case Item::OverlayLayout:
        case Item::OverlayFontSize:
        case Item::OverlayFillColour:
        case Item::OverlayOutlineColour:
            return modern && g_fps_overlay_enabled;
        case Item::MetricFrameTime:
        case Item::MetricSimulation:
        case Item::MetricGraphics:
        case Item::MetricVi:
        case Item::MetricInterpolation:
        case Item::MetricAudio:
        case Item::MetricTarget:
        case Item::MetricResolution:
            return modern && g_fps_overlay_enabled && g_fps_overlay_detail == 3;
        default:
            // Everything else on these pages is Modern-only in the launcher.
            return handles(id) && modern;
    }
}

std::string dkr::runtime::ui::display::value(options_menu::ItemId id) {
    using Item = options_menu::ItemId;
    namespace enh = dkr::runtime::enhancements;
    namespace text = dkr::accessibility::text;
    const GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    if (const auto scenery = MenuGraphicsScenery(id)) return MenuGraphicsToggle(scenery->requested());
    if (const auto distance = MenuGraphicsDistanceOf(id)) return text::display_times(distance->get());
    if (bool* metric = MenuGraphicsMetric(id)) return MenuGraphicsToggle(*metric);
    if (const ImVec4* colour = MenuGraphicsColourSetting(id)) {
        const std::optional<int> named = MenuGraphicsColourIndex(*colour);
        return named ? MenuGraphicsChoice(id, *named) : text::phrase(text::Phrase::Custom);
    }
#if defined(DKR_ENHANCED_WATER)
    namespace water = dkr::runtime::water::enhanced;
    if (const auto percent = MenuGraphicsWaterPercentOf(id)) return text::percent(percent->setting->load());
    if (std::atomic<int>* detail = MenuGraphicsWaterDetail(id)) {
        return MenuGraphicsChoice(id, std::clamp(detail->load() + 1, 0, 3));
    }
    if (std::atomic<bool>* toggle = MenuGraphicsWaterToggle(id)) return MenuGraphicsToggle(toggle->load());
#endif
    switch (id) {
        case Item::Presentation: return MenuGraphicsChoice(id, static_cast<int>(enh::presentation_profile()));
        case Item::WindowMode: return MenuGraphicsChoice(id, static_cast<int>(config.wm_option));
        case Item::PerformancePreset: return MenuGraphicsPresetName(MenuGraphicsPendingPreset());
        case Item::Resolution: return MenuGraphicsChoice(id, static_cast<int>(config.res_option));
        case Item::AspectRatio: return MenuGraphicsChoice(id, config.ar_option == AspectRatio::Original ? 0 : 1);
        case Item::FrameRate:
            return text::display_frame_rate(g_modern_refresh_mode == RefreshRate::Manual
                                                ? std::optional<int>(g_modern_refresh_target)
                                                : std::nullopt);
        case Item::GraphicsApi:
            return MenuGraphicsChoice(
                id, static_cast<int>(kMenuGraphicsApis[static_cast<std::size_t>(MenuGraphicsApiIndex())]));
        case Item::AntiAliasing: return MenuGraphicsChoice(id, static_cast<int>(config.msaa_option));
        case Item::Anisotropy: {
            std::string level = MenuGraphicsChoice(id, MenuGraphicsAnisotropyIndex());
            // The sampler level is set when the renderer starts.
            if (static_cast<std::uint32_t>(enh::anisotropy_level()) != RT64::getDefaultSamplerAnisotropy()) {
                return text::display_applies_next_game(level);
            }
            return level;
        }
        case Item::ColourPrecision: return MenuGraphicsChoice(id, static_cast<int>(config.hpfb_option));
        case Item::Supersampling: return text::display_supersampling(std::clamp(config.ds_option, 1, 4));
        case Item::TextureSharpness: return text::display_texture_bias(enh::texture_lod_bias_hundredths());
        case Item::TextureShimmering: {
            const bool requested = enh::generated_mipmaps_requested();
            std::string state = MenuGraphicsToggle(requested);
            // As the launcher's "Change pending" note.
            if (requested != RT64::generatedMipSessionEnabled()) return text::display_applies_next_game(state);
            return state;
        }
        case Item::HudLayout: {
            namespace hud = dkr::runtime::hud;
            return MenuGraphicsChoice(id, hud::basic_mode(hud::mode()) == hud::LayoutMode::FitToViewport ? 1 : 0);
        }
        case Item::FieldOfView: return text::display_degrees(enh::fov_offset());
        case Item::ModernCamera: return MenuGraphicsToggle(dkr::runtime::modern_camera::requested());
        case Item::LookBack: return MenuGraphicsToggle(dkr::runtime::modern_camera::look_back_requested());
        case Item::CrtFilter: return MenuGraphicsToggle(g_crt_enabled);
        case Item::CrtImage: {
            if (g_crt_filters.empty()) return {};
            const int index = std::clamp(g_crt_filter_index, 0, static_cast<int>(g_crt_filters.size()) - 1);
            return MenuGraphicsUpper(g_crt_filters[static_cast<std::size_t>(index)].label);
        }
        case Item::CrtScaling: return MenuGraphicsChoice(id, std::clamp(g_crt_scale_mode, 0, 1));
        case Item::CrtStrength: return text::percent(MenuGraphicsCrtDensity());
        case Item::VisibilityMargin: return text::percent(enh::frustum_guard_percent());
        case Item::SceneryCoverage: return MenuGraphicsChoice(id, static_cast<int>(enh::scenery_retention_mode()));
#if defined(DKR_ENHANCED_WATER)
        case Item::WaterPreset: return MenuGraphicsChoice(id, water::preset());
        case Item::Reflections: return MenuGraphicsChoice(id, static_cast<int>(water::requested_reflection.load()));
        case Item::LavaGlow: return MenuGraphicsToggle(water::lava_lighting_enabled.load() != 0);
#endif
        case Item::OverlayShow: return MenuGraphicsToggle(g_fps_overlay_enabled);
        case Item::OverlayPosition: return MenuGraphicsChoice(id, g_fps_overlay_position);
        case Item::OverlayDetail: return MenuGraphicsChoice(id, g_fps_overlay_detail);
        case Item::OverlayLayout: return MenuGraphicsChoice(id, g_fps_overlay_single_row ? 1 : 0);
        case Item::OverlayFontSize: return text::display_pixels(g_fps_font_size);
        default: return {};
    }
}

bool dkr::runtime::ui::display::adjust(options_menu::ItemId id, int step) {
    using Item = options_menu::ItemId;
    namespace enh = dkr::runtime::enhancements;
    if (step == 0) return false;
    step = step < 0 ? -1 : 1;
    if (const auto scenery = MenuGraphicsScenery(id)) {
        scenery->set(!scenery->requested());
        SaveSettings();
        return true;
    }
    if (const auto distance = MenuGraphicsDistanceOf(id)) {
        const int current = distance->get();
        const int next = std::clamp(current + step, enh::kMinimumViewDistanceMultiplier, distance->maximum);
        if (next == current) return false;
        distance->set(next);
        SaveSettings();
        return true;
    }
    if (bool* metric = MenuGraphicsMetric(id)) {
        *metric = !*metric;
        SaveSettings();
        return true;
    }
    if (ImVec4* colour = MenuGraphicsColourSetting(id)) {
        const int count = static_cast<int>(kMenuGraphicsColours.size());
        const std::optional<int> named = MenuGraphicsColourIndex(*colour);
        // From a colour outside the list, the first or last named one.
        const int next = named ? MenuGraphicsWrap(*named, step, count) : (step > 0 ? 0 : count - 1);
        const MenuGraphicsColour& chosen = kMenuGraphicsColours[static_cast<std::size_t>(next)];
        *colour = ImVec4{static_cast<float>(chosen.r) / 255.0F, static_cast<float>(chosen.g) / 255.0F,
                         static_cast<float>(chosen.b) / 255.0F, 1.0F};
        SaveSettings();
        return true;
    }
#if defined(DKR_ENHANCED_WATER)
    namespace water = dkr::runtime::water::enhanced;
    if (const auto percent = MenuGraphicsWaterPercentOf(id)) {
        const int current = percent->setting->load();
        const int next = std::clamp(current + step * kMenuGraphicsPercentStep, percent->low, percent->high);
        if (next == current) return false;
        percent->setting->store(next);
        SaveSettings();
        return true;
    }
    if (std::atomic<int>* detail = MenuGraphicsWaterDetail(id)) {
        const int current = std::clamp(detail->load() + 1, 0, 3);
        detail->store(MenuGraphicsWrap(current, step, 4) - 1);
        SaveSettings();
        return true;
    }
    if (std::atomic<bool>* toggle = MenuGraphicsWaterToggle(id)) {
        toggle->store(!toggle->load());
        SaveSettings();
        return true;
    }
#endif
    switch (id) {
        case Item::Presentation: {
            const int current = static_cast<int>(enh::presentation_profile());
            dkr::runtime::ui::set_presentation_profile(enh::normalise_presentation_profile(MenuGraphicsWrap(current, step, 2)));
            return true;
        }
        case Item::WindowMode:
            return MenuGraphicsStepConfig(&GraphicsConfig::wm_option, static_cast<int>(WindowMode::OptionCount), step);
        case Item::PerformancePreset: {
            // Only the pending pick changes; A applies it. CUSTOM is shown, not picked.
            const int current = MenuGraphicsPendingPreset();
            g_menu_graphics_preset = current >= kMenuGraphicsCustomPreset
                ? (step > 0 ? 0 : kMenuGraphicsPresetCount - 1)
                : MenuGraphicsWrap(current, step, kMenuGraphicsPresetCount);
            return g_menu_graphics_preset != current;
        }
        case Item::Resolution:
            return MenuGraphicsStepConfig(&GraphicsConfig::res_option, static_cast<int>(Resolution::OptionCount), step);
        case Item::AspectRatio: {
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            config.ar_option = config.ar_option == AspectRatio::Original ? AspectRatio::Expand : AspectRatio::Original;
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::FrameRate: {
            const int next = MenuGraphicsNextFrameRate(step);
            if (next == 0) {
                g_modern_refresh_mode = RefreshRate::Display;
            } else {
                g_modern_refresh_mode = RefreshRate::Manual;
                g_modern_refresh_target =
                    enh::clamp_presentation_rate(kMenuGraphicsFrameRates[static_cast<std::size_t>(next - 1)]);
            }
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::GraphicsApi: {
            const int count = static_cast<int>(kMenuGraphicsApis.size());
            if (count <= 1) return false;
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            config.api_option = kMenuGraphicsApis[static_cast<std::size_t>(
                MenuGraphicsWrap(MenuGraphicsApiIndex(), step, count))];
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::AntiAliasing:
            return MenuGraphicsStepConfig(&GraphicsConfig::msaa_option, static_cast<int>(Antialiasing::OptionCount),
                                          step);
        case Item::Anisotropy: {
            const int next =
                MenuGraphicsWrap(MenuGraphicsAnisotropyIndex(), step, static_cast<int>(kMenuGraphicsAnisotropy.size()));
            enh::set_anisotropy_level(kMenuGraphicsAnisotropy[static_cast<std::size_t>(next)]);
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::ColourPrecision:
            return MenuGraphicsStepConfig(&GraphicsConfig::hpfb_option,
                                          static_cast<int>(HighPrecisionFramebuffer::OptionCount), step);
        case Item::Supersampling: {
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            const int current = std::clamp(config.ds_option, 1, 4);
            const int next = std::clamp(current + step, 1, 4);
            if (next == current) return false;
            config.ds_option = next;
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::TextureSharpness: {
            const int current = enh::texture_lod_bias_hundredths();
            const int next = std::clamp(current + step * kMenuGraphicsBiasStep, enh::kMinimumTextureLodBiasHundredths,
                                        enh::kMaximumTextureLodBiasHundredths);
            if (next == current) return false;
            enh::set_texture_lod_bias_hundredths(next);
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::TextureShimmering: {
            enh::set_generated_mipmaps_requested(!enh::generated_mipmaps_requested());
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            MenuGraphicsCommit(config);
            return true;
        }
        case Item::HudLayout: {
            namespace hud = dkr::runtime::hud;
            const bool fit = hud::basic_mode(hud::mode()) == hud::LayoutMode::FitToViewport;
            // Saves hud-placement.ini; on failure the previous layout stays.
            return hud::apply_basic_mode(fit ? hud::LayoutMode::Original : hud::LayoutMode::FitToViewport);
        }
        case Item::FieldOfView: {
            const int current = enh::fov_offset();
            const int next = std::clamp(current + step, enh::kMinimumFovOffset, enh::kMaximumFovOffset);
            if (next == current) return false;
            enh::set_fov_offset(next);
            SaveSettings();
            return true;
        }
        case Item::ModernCamera:
            dkr::runtime::modern_camera::set_enabled(!dkr::runtime::modern_camera::requested());
            SaveSettings();
            return true;
        case Item::LookBack:
            dkr::runtime::modern_camera::set_look_back_enabled(!dkr::runtime::modern_camera::look_back_requested());
            SaveSettings();
            return true;
        case Item::CrtFilter:
            g_crt_enabled = !g_crt_enabled;
            SaveSettings();
            return true;
        case Item::CrtImage: {
            const int count = static_cast<int>(g_crt_filters.size());
            if (count <= 1) return false;
            g_crt_filter_index = MenuGraphicsWrap(std::clamp(g_crt_filter_index, 0, count - 1), step, count);
            SaveSettings();
            return true;
        }
        case Item::CrtScaling:
            g_crt_scale_mode = MenuGraphicsWrap(std::clamp(g_crt_scale_mode, 0, 1), step, 2);
            SaveSettings();
            return true;
        case Item::CrtStrength: {
            const int current = MenuGraphicsCrtDensity();
            const int next = std::clamp(current + step * kMenuGraphicsPercentStep, 0, 100);
            if (next == current) return false;
            g_crt_strength = static_cast<float>(next) / 100.0F;
            SaveSettings();
            return true;
        }
        case Item::VisibilityMargin: {
            const int current = enh::frustum_guard_percent();
            const int next = std::clamp(current + step * kMenuGraphicsPercentStep, enh::kMinimumFrustumGuardPercent,
                                        enh::kMaximumFrustumGuardPercent);
            if (next == current) return false;
            enh::set_frustum_guard_percent(next);
            SaveSettings();
            return true;
        }
        case Item::SceneryCoverage: {
            const int current = static_cast<int>(enh::scenery_retention_mode());
            enh::set_scenery_retention_mode(enh::normalise_scenery_retention_mode(MenuGraphicsWrap(current, step, 4)));
            SaveSettings();
            return true;
        }
#if defined(DKR_ENHANCED_WATER)
        case Item::WaterPreset: {
            // ORIGINAL to HIGH. CUSTOM is shown when tuned by hand; choosing it
            // changes nothing, so it is stepped over.
            const int current = water::preset();
            const int next = current > 3 ? (step > 0 ? 0 : 3) : MenuGraphicsWrap(current, step, 4);
            water::apply_preset(next);
            SaveSettings();
            return true;
        }
        case Item::Reflections:
            water::set_reflection(MenuGraphicsWrap(static_cast<int>(water::requested_reflection.load()), step, 3));
            SaveSettings();
            return true;
        case Item::LavaGlow:
            water::lava_lighting_enabled.store(water::lava_lighting_enabled.load() != 0 ? 0 : 1);
            SaveSettings();
            return true;
#endif
        case Item::OverlayShow:
            g_fps_overlay_enabled = !g_fps_overlay_enabled;
            SaveSettings();
            return true;
        case Item::OverlayPosition:
            g_fps_overlay_position = MenuGraphicsWrap(std::clamp(g_fps_overlay_position, 0, 3), step, 4);
            SaveSettings();
            return true;
        case Item::OverlayDetail:
            g_fps_overlay_detail = MenuGraphicsWrap(std::clamp(g_fps_overlay_detail, 0, 3), step, 4);
            SaveSettings();
            return true;
        case Item::OverlayLayout:
            g_fps_overlay_single_row = !g_fps_overlay_single_row;
            SaveSettings();
            return true;
        case Item::OverlayFontSize: {
            const int current = g_fps_font_size;
            const int next = std::clamp(current + step * kMenuGraphicsFontStep, 16, 64);
            if (next == current) return false;
            g_fps_font_size = next;
            SaveSettings();
            return true;
        }
        default:
            return false;
    }
}

std::optional<std::string> dkr::runtime::ui::display::activate(options_menu::ItemId id) {
    using Item = options_menu::ItemId;
    namespace enh = dkr::runtime::enhancements;
    namespace text = dkr::accessibility::text;
    switch (id) {
        case Item::PerformancePreset: {
            // As the launcher's APPLY PRESET.
            const int pending = MenuGraphicsPendingPreset();
            if (pending >= kMenuGraphicsCustomPreset) return std::nullopt;
            namespace graphics_presets = dkr::runtime::mobile_graphics;
            GraphicsConfig config = ultramodern::renderer::get_graphics_config();
            if (!enh::modern_presentation_enabled()) {
                enh::set_presentation_profile(enh::PresentationProfile::Modern);
                ApplyProfileGraphics(config, enh::PresentationProfile::Modern);
            }
            const auto chosen = static_cast<graphics_presets::Preset>(pending);
            graphics_presets::apply_preset(config, chosen);
            enh::set_anisotropy_level(graphics_presets::preset_anisotropy(chosen));
            RememberModernGraphics(config);
            MenuGraphicsCommit(config);
            return text::display_preset_applied(MenuGraphicsPresetName(pending));
        }
#if defined(DKR_ENHANCED_WATER)
        case Item::ResetWaterAppearance:
            dkr::runtime::water::enhanced::reset_appearance();
            SaveSettings();
            return text::display_done(id);
        case Item::ResetWaterQuality:
            dkr::runtime::water::enhanced::reset_tuning();
            SaveSettings();
            return text::display_done(id);
#endif
        default:
            return std::nullopt;
    }
}
