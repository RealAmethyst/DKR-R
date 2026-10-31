#include "options_menu.hpp"

#include "accessibility/localization.hpp"

#include <algorithm>
#include <array>

namespace dkr::options_menu {
namespace {

constexpr std::array kRootItems{
    ItemDef{ItemId::GeneralOptions, ItemKind::Page, PageId::General},
    ItemDef{ItemId::DisplayOptions, ItemKind::Page, PageId::Display},
    ItemDef{ItemId::AudioOptions, ItemKind::Page, PageId::Audio},
    ItemDef{ItemId::SaveOptions, ItemKind::Screen},
    ItemDef{ItemId::MagicCodes, ItemKind::Screen},
    ItemDef{ItemId::Return, ItemKind::Return},
};

constexpr std::array kGeneralItems{
    ItemDef{ItemId::Language, ItemKind::Setting},
    ItemDef{ItemId::Subtitles, ItemKind::Setting},
};

constexpr std::array kDisplayItems{
    ItemDef{ItemId::Presentation, ItemKind::Setting},
    ItemDef{ItemId::WindowMode, ItemKind::Setting},
    ItemDef{ItemId::PerformancePreset, ItemKind::Setting},
    ItemDef{ItemId::Resolution, ItemKind::Setting},
    ItemDef{ItemId::AspectRatio, ItemKind::Setting},
    ItemDef{ItemId::FrameRate, ItemKind::Setting},
    ItemDef{ItemId::AdvancedGraphics, ItemKind::Page, PageId::AdvancedGraphics},
    ItemDef{ItemId::HudAndCamera, ItemKind::Page, PageId::HudAndCamera},
    ItemDef{ItemId::Scenery, ItemKind::Page, PageId::Scenery},
    ItemDef{ItemId::WaterAndLava, ItemKind::Page, PageId::WaterAndLava},
    ItemDef{ItemId::PerformanceOverlay, ItemKind::Page, PageId::PerformanceOverlay},
};

constexpr std::array kAdvancedGraphicsItems{
    ItemDef{ItemId::GraphicsApi, ItemKind::Setting},
    ItemDef{ItemId::AntiAliasing, ItemKind::Setting},
    ItemDef{ItemId::Anisotropy, ItemKind::Setting},
    ItemDef{ItemId::ColourPrecision, ItemKind::Setting},
    ItemDef{ItemId::Supersampling, ItemKind::Setting},
    ItemDef{ItemId::TextureSharpness, ItemKind::Setting},
    ItemDef{ItemId::TextureShimmering, ItemKind::Setting},
};

constexpr std::array kHudAndCameraItems{
    ItemDef{ItemId::HudLayout, ItemKind::Setting},
    ItemDef{ItemId::FieldOfView, ItemKind::Setting},
    ItemDef{ItemId::ModernCamera, ItemKind::Setting},
    ItemDef{ItemId::LookBack, ItemKind::Setting},
    ItemDef{ItemId::CrtFilter, ItemKind::Setting},
    ItemDef{ItemId::CrtImage, ItemKind::Setting},
    ItemDef{ItemId::CrtScaling, ItemKind::Setting},
    ItemDef{ItemId::CrtStrength, ItemKind::Setting},
};

constexpr std::array kSceneryItems{
    ItemDef{ItemId::MaximumDetail, ItemKind::Setting},
    ItemDef{ItemId::KeepHubScenery, ItemKind::Setting},
    ItemDef{ItemId::KeepTrackScenery, ItemKind::Setting},
    ItemDef{ItemId::KeepMinigameScenery, ItemKind::Setting},
    ItemDef{ItemId::ReducePopIn, ItemKind::Setting},
    ItemDef{ItemId::VisibilityMargin, ItemKind::Setting},
    ItemDef{ItemId::ViewDistance, ItemKind::Setting},
    ItemDef{ItemId::SceneryCoverage, ItemKind::Setting},
    ItemDef{ItemId::AnimatedDistance, ItemKind::Setting},
    ItemDef{ItemId::EffectsDistance, ItemKind::Setting},
    ItemDef{ItemId::WaterLavaDistance, ItemKind::Setting},
};

constexpr std::array kWaterAndLavaItems{
    ItemDef{ItemId::WaterPreset, ItemKind::Setting},
    ItemDef{ItemId::Reflections, ItemKind::Setting},
    ItemDef{ItemId::RacerWakes, ItemKind::Setting},
    ItemDef{ItemId::WaterSpray, ItemKind::Setting},
    ItemDef{ItemId::LavaGlow, ItemKind::Setting},
    ItemDef{ItemId::LavaIntensity, ItemKind::Setting},
    ItemDef{ItemId::LavaRange, ItemKind::Setting},
    ItemDef{ItemId::LavaBrightness, ItemKind::Setting},
    ItemDef{ItemId::LavaPulseStrength, ItemKind::Setting},
    ItemDef{ItemId::LavaPulseSpeed, ItemKind::Setting},
    ItemDef{ItemId::AdvancedWater, ItemKind::Page, PageId::AdvancedWater},
};

constexpr std::array kAdvancedWaterItems{
    ItemDef{ItemId::ReflectionDetail, ItemKind::Setting},
    ItemDef{ItemId::WakeDetail, ItemKind::Setting},
    ItemDef{ItemId::SprayDetail, ItemKind::Setting},
    ItemDef{ItemId::ReflectionDistance, ItemKind::Setting},
    ItemDef{ItemId::ReflectionStrength, ItemKind::Setting},
    ItemDef{ItemId::SurfaceRoughness, ItemKind::Setting},
    ItemDef{ItemId::RippleStrength, ItemKind::Setting},
    ItemDef{ItemId::FoamIntensity, ItemKind::Setting},
    ItemDef{ItemId::WakeVisibility, ItemKind::Setting},
    ItemDef{ItemId::WaterClarity, ItemKind::Setting},
    ItemDef{ItemId::RefractionStrength, ItemKind::Setting},
    ItemDef{ItemId::WaterfallDetail, ItemKind::Setting},
    ItemDef{ItemId::TrackColours, ItemKind::Setting},
    ItemDef{ItemId::TintRed, ItemKind::Setting},
    ItemDef{ItemId::TintGreen, ItemKind::Setting},
    ItemDef{ItemId::TintBlue, ItemKind::Setting},
    ItemDef{ItemId::ResetWaterAppearance, ItemKind::Action},
    ItemDef{ItemId::ResetWaterQuality, ItemKind::Action},
    ItemDef{ItemId::RecordWaterPerformance, ItemKind::Setting},
};

constexpr std::array kPerformanceOverlayItems{
    ItemDef{ItemId::OverlayShow, ItemKind::Setting},
    ItemDef{ItemId::OverlayPosition, ItemKind::Setting},
    ItemDef{ItemId::OverlayDetail, ItemKind::Setting},
    ItemDef{ItemId::OverlayLayout, ItemKind::Setting},
    ItemDef{ItemId::OverlayFontSize, ItemKind::Setting},
    ItemDef{ItemId::OverlayFillColour, ItemKind::Setting},
    ItemDef{ItemId::OverlayOutlineColour, ItemKind::Setting},
    ItemDef{ItemId::MetricFrameTime, ItemKind::Setting},
    ItemDef{ItemId::MetricSimulation, ItemKind::Setting},
    ItemDef{ItemId::MetricGraphics, ItemKind::Setting},
    ItemDef{ItemId::MetricVi, ItemKind::Setting},
    ItemDef{ItemId::MetricInterpolation, ItemKind::Setting},
    ItemDef{ItemId::MetricAudio, ItemKind::Setting},
    ItemDef{ItemId::MetricTarget, ItemKind::Setting},
    ItemDef{ItemId::MetricResolution, ItemKind::Setting},
};

// Volume sliders first, then the other sound settings.
constexpr std::array kAudioItems{
    ItemDef{ItemId::MasterVolume, ItemKind::Setting},
    ItemDef{ItemId::MusicVolume, ItemKind::Setting},
    ItemDef{ItemId::SfxVolume, ItemKind::Setting},
    ItemDef{ItemId::VehiclesVolume, ItemKind::Setting},
    ItemDef{ItemId::NatureVolume, ItemKind::Setting},
    ItemDef{ItemId::OutputDevice, ItemKind::Setting},
    ItemDef{ItemId::SoundMode, ItemKind::Setting},
    ItemDef{ItemId::MultiplayerMusic, ItemKind::Setting},
    ItemDef{ItemId::EqualiserPreset, ItemKind::Setting},
    ItemDef{ItemId::EqualiserBass, ItemKind::Setting},
    ItemDef{ItemId::EqualiserMid, ItemKind::Setting},
    ItemDef{ItemId::EqualiserTreble, ItemKind::Setting},
};

constexpr std::array kPages{
    PageDef{PageId::Root, kRootItems},
    PageDef{PageId::General, kGeneralItems},
    PageDef{PageId::Display, kDisplayItems},
    PageDef{PageId::AdvancedGraphics, kAdvancedGraphicsItems},
    PageDef{PageId::HudAndCamera, kHudAndCameraItems},
    PageDef{PageId::Scenery, kSceneryItems},
    PageDef{PageId::WaterAndLava, kWaterAndLavaItems},
    PageDef{PageId::AdvancedWater, kAdvancedWaterItems},
    PageDef{PageId::PerformanceOverlay, kPerformanceOverlayItems},
    PageDef{PageId::Audio, kAudioItems},
};

std::span<const ItemDef> page_items(PageId page) {
    for (const PageDef& def : kPages) {
        if (def.id == page) return def.items;
    }
    return {};
}

} // namespace

std::span<const PageDef> pages() { return kPages; }

std::string item_text(const std::string& label, const std::string& value) {
    if (label.empty()) return value;
    if (value.empty()) return label;
    return label + ", " + value;
}

std::vector<ItemDef> Controller::visible_items(const Settings& settings, PageId page) const {
    std::vector<ItemDef> items;
    for (const ItemDef& item : page_items(page)) {
        if (settings.visible(item.id)) items.push_back(item);
    }
    return items;
}

std::optional<std::size_t> Controller::cursor_index(const std::vector<ItemDef>& items) const {
    if (items.empty()) return std::nullopt;
    const auto found = std::find_if(items.begin(), items.end(),
                                    [&](const ItemDef& item) { return item.id == stack_.back().cursor; });
    // A setting elsewhere can hide the remembered item; fall back to the top.
    return found == items.end() ? 0U : static_cast<std::size_t>(found - items.begin());
}

std::vector<accessibility::Utterance> Controller::item_speech(const Settings& settings, bool interrupt) const {
    const std::vector<ItemDef> items = visible_items(settings, page());
    const std::optional<std::size_t> index = cursor_index(items);
    if (!index) return {};
    const ItemId id = items[*index].id;
    const std::string text = item_text(settings.label(id), settings.value(id));
    if (text.empty()) return {};
    return {{accessibility::text::item(text, *index + 1U, items.size()), interrupt}};
}

std::vector<accessibility::Utterance> Controller::page_speech(const Settings& settings) const {
    const std::string heading = settings.heading(page());
    if (heading.empty()) return item_speech(settings, true);
    std::vector<accessibility::Utterance> speech{{heading, true}};
    for (auto& item : item_speech(settings, false)) speech.push_back(std::move(item));
    return speech;
}

void Controller::enter(PageId page, const Settings& settings) {
    const std::vector<ItemDef> items = visible_items(settings, page);
    stack_.push_back({page, items.empty() ? page_items(page).front().id : items.front().id});
}

Result Controller::open(const Settings& settings) {
    Result result;
    if (left_for_) {
        // Back from Save Options or Magic Codes: the root page is still open
        // underneath, so only the item that left is spoken.
        stack_.resize(1);
        stack_.back().cursor = *left_for_;
        left_for_.reset();
        result.speech = item_speech(settings, true);
        return result;
    }
    stack_.clear();
    enter(PageId::Root, settings);
    result.speech = page_speech(settings);
    return result;
}

Result Controller::handle(Input input, Settings& settings) {
    Result result;
    const std::vector<ItemDef> items = visible_items(settings, page());
    const std::optional<std::size_t> index = cursor_index(items);
    if (!index) return result;
    const ItemDef& item = items[*index];
    switch (input) {
        case Input::Up:
        case Input::Down: {
            const std::size_t next = input == Input::Up ? (*index == 0U ? 0U : *index - 1U)
                                                        : std::min(*index + 1U, items.size() - 1U);
            if (next == *index) break;
            stack_.back().cursor = items[next].id;
            result.sound = Sound::Move;
            result.speech = item_speech(settings, true);
            break;
        }
        case Input::Left:
        case Input::Right:
            if (item.kind != ItemKind::Setting || !settings.adjust(item.id, input == Input::Left ? -1 : 1)) break;
            result.sound = Sound::Move;
            result.changed = true;
            result.speech = item_speech(settings, true);
            break;
        case Input::Accept:
            if (item.kind == ItemKind::Page) {
                stack_.back().cursor = item.id;
                enter(item.page, settings);
                result.sound = Sound::Select;
                result.speech = page_speech(settings);
            } else if (item.kind == ItemKind::Screen) {
                left_for_ = item.id;
                result.sound = Sound::Select;
                result.leave = item.id;
            } else if (item.kind == ItemKind::Return) {
                result.sound = Sound::Back;
                result.leave = item.id;
            } else if (const std::optional<std::string> done = settings.activate(item.id)) {
                result.sound = Sound::Select;
                result.changed = true;
                if (!done->empty()) result.speech = {{*done, true}};
            }
            break;
        case Input::Back:
            result.sound = Sound::Back;
            if (stack_.size() > 1U) {
                stack_.pop_back();
                result.speech = item_speech(settings, true);
            } else {
                result.leave = ItemId::Return;
            }
            break;
    }
    return result;
}

std::vector<Row> Controller::rows(const Settings& settings) const {
    const std::vector<ItemDef> items = visible_items(settings, page());
    const std::optional<std::size_t> index = cursor_index(items);
    std::vector<Row> rows;
    for (std::size_t i = 0; i < items.size(); ++i) {
        rows.push_back({items[i].id, item_text(settings.label(items[i].id), settings.value(items[i].id)),
                        index && *index == i});
    }
    return rows;
}

} // namespace dkr::options_menu
