#pragma once

#include "accessibility/menu_speech_policy.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The redesigned Options menu: the game's own settings and DKR-R's settings
// sorted into pages. Pages and their order are data (kPages), so items can be
// moved between pages without touching the controller.
namespace dkr::options_menu {

enum class PageId {
    Root,
    General,
    Display,
    AdvancedGraphics,
    HudAndCamera,
    Scenery,
    WaterAndLava,
    AdvancedWater,
    PerformanceOverlay,
    Audio,
    AccessibilityOptions,
    AudioCues,
};

enum class ItemId {
    // Root
    GeneralOptions,
    DisplayOptions,
    AudioOptions,
    SaveOptions,
    MagicCodes,
    AccessibilityOptions,
    Return,
    // General
    Language,
    Subtitles,
    // Display (DKR-R's graphics settings; display_bridge.inl)
    Presentation,
    WindowMode,
    PerformancePreset,
    Resolution,
    AspectRatio,
    FrameRate,
    AdvancedGraphics,
    HudAndCamera,
    Scenery,
    WaterAndLava,
    PerformanceOverlay,
    // Advanced graphics
    GraphicsApi,
    AntiAliasing,
    Anisotropy,
    ColourPrecision,
    Supersampling,
    TextureSharpness,
    TextureShimmering,
    // HUD and camera
    HudLayout,
    FieldOfView,
    ModernCamera,
    LookBack,
    CrtFilter,
    CrtImage,
    CrtScaling,
    CrtStrength,
    // Scenery
    MaximumDetail,
    KeepHubScenery,
    KeepTrackScenery,
    KeepMinigameScenery,
    ReducePopIn,
    VisibilityMargin,
    ViewDistance,
    SceneryCoverage,
    AnimatedDistance,
    EffectsDistance,
    WaterLavaDistance,
    // Water and lava
    WaterPreset,
    Reflections,
    RacerWakes,
    WaterSpray,
    LavaGlow,
    LavaIntensity,
    LavaRange,
    LavaBrightness,
    LavaPulseStrength,
    LavaPulseSpeed,
    AdvancedWater,
    // Advanced water
    ReflectionDetail,
    WakeDetail,
    SprayDetail,
    ReflectionDistance,
    ReflectionStrength,
    SurfaceRoughness,
    RippleStrength,
    FoamIntensity,
    WakeVisibility,
    WaterClarity,
    RefractionStrength,
    WaterfallDetail,
    TrackColours,
    TintRed,
    TintGreen,
    TintBlue,
    ResetWaterAppearance,
    ResetWaterQuality,
    RecordWaterPerformance,
    // Performance overlay
    OverlayShow,
    OverlayPosition,
    OverlayDetail,
    OverlayLayout,
    OverlayFontSize,
    OverlayFillColour,
    OverlayOutlineColour,
    MetricFrameTime,
    MetricSimulation,
    MetricGraphics,
    MetricVi,
    MetricInterpolation,
    MetricAudio,
    MetricTarget,
    MetricResolution,
    // Audio
    MasterVolume,
    MusicVolume,
    SfxVolume,
    VehiclesVolume,
    NatureVolume,
    OutputDevice,
    SoundMode,
    MultiplayerMusic,
    EqualiserPreset,
    EqualiserBass,
    EqualiserMid,
    EqualiserTreble,
    // Accessibility
    AudioCues,
    // Audio cues (race_cues/cue_settings.hpp)
    TrackEdgeSound,
    TrackEdgeVolume,
    TrackEdgeDistance,
    TurnSignal,
    TurnSignalVolume,
    TurnSignalDirection,
    TurnWarningTime,
    BeaconSound,
    BeaconVolume,
};

enum class ItemKind {
    Page,    // opens another page of this menu
    Screen,  // leaves for one of the game's own screens
    Return,  // leaves the menu
    Setting, // left/right changes its value; some also act on A
    Action,  // A does something (a reset)
};

struct ItemDef {
    ItemId id;
    ItemKind kind;
    PageId page = PageId::Root; // the page a Page item opens
};

struct PageDef {
    PageId id;
    std::span<const ItemDef> items;
};

std::span<const PageDef> pages();

// What each item shows and does. The game side implements it with the game's
// own text and settings; tests use a fake.
class Settings {
public:
    virtual ~Settings() = default;
    virtual bool visible(ItemId) const { return true; }
    // Text as drawn and spoken; empty when it cannot be verified.
    virtual std::string heading(PageId) const = 0;
    virtual std::string label(ItemId) const = 0;
    virtual std::string value(ItemId) const { return {}; }
    // One step left (-1) or right (+1); true when the value changed.
    virtual bool adjust(ItemId, int) { return false; }
    // A on a Setting or Action. Returns what to say when it did something
    // (a preset applied, a reset done); nullopt when A does nothing here.
    virtual std::optional<std::string> activate(ItemId) { return std::nullopt; }
};

enum class Input { Up, Down, Left, Right, Accept, Back };
enum class Sound { None, Move, Select, Back };

struct Result {
    Sound sound = Sound::None;
    std::vector<accessibility::Utterance> speech;
    std::optional<ItemId> leave; // a Screen or Return item was chosen
    bool changed = false;        // a setting changed; persist settings
};

// One visible row of the current page.
struct Row {
    ItemId id;
    std::string text; // label and value, as drawn
    bool selected = false;
};

class Controller {
public:
    // The menu was opened. Arriving back from one of the game's own screens
    // resumes on the item that left; otherwise it starts on the first page.
    Result open(const Settings& settings);
    Result handle(Input input, Settings& settings);

    PageId page() const { return stack_.back().page; }
    std::string heading(const Settings& settings) const { return settings.heading(page()); }
    std::vector<Row> rows(const Settings& settings) const;

private:
    struct Level {
        PageId page;
        ItemId cursor;
    };

    std::vector<ItemDef> visible_items(const Settings& settings, PageId page) const;
    std::optional<std::size_t> cursor_index(const std::vector<ItemDef>& items) const;
    std::vector<accessibility::Utterance> item_speech(const Settings& settings, bool interrupt) const;
    std::vector<accessibility::Utterance> page_speech(const Settings& settings) const;
    void enter(PageId page, const Settings& settings);

    std::vector<Level> stack_{{PageId::Root, ItemId::GeneralOptions}};
    std::optional<ItemId> left_for_; // the Screen item that left the menu
};

// "label, value" with absent parts omitted.
std::string item_text(const std::string& label, const std::string& value);

} // namespace dkr::options_menu
