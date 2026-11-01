#include "options_menu/options_menu.hpp"

#include "accessibility/localization.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace dkr::options_menu;
using dkr::accessibility::Utterance;
namespace text = dkr::accessibility::text;

namespace {

class FakeSettings final : public Settings {
public:
    bool modern = false;
    int music = 80;
    bool subtitles = false;
    bool edge = true;
    int warning = 20;

    bool visible(ItemId id) const override {
        switch (id) {
            case ItemId::VehiclesVolume:
            case ItemId::NatureVolume:
            case ItemId::MultiplayerMusic:
            case ItemId::EqualiserPreset:
            case ItemId::EqualiserBass:
            case ItemId::EqualiserMid:
            case ItemId::EqualiserTreble:
                return modern;
            default:
                return true;
        }
    }
    std::string heading(PageId page) const override {
        switch (page) {
            case PageId::Root: return "OPTIONS";
            case PageId::General: return "GENERAL OPTIONS";
            case PageId::Display: return "DISPLAY OPTIONS";
            case PageId::Audio: return "AUDIO OPTIONS";
            case PageId::AccessibilityOptions: return "ACCESSIBILITY OPTIONS";
            case PageId::AudioCues: return "AUDIO CUES";
            default: return {};
        }
    }
    std::string label(ItemId id) const override {
        static const std::map<ItemId, std::string> labels{
            {ItemId::GeneralOptions, "GENERAL OPTIONS"}, {ItemId::DisplayOptions, "DISPLAY OPTIONS"},
            {ItemId::AudioOptions, "AUDIO OPTIONS"},
            {ItemId::SaveOptions, "SAVE OPTIONS"}, {ItemId::MagicCodes, "MAGIC CODES"},
            {ItemId::Return, "RETURN"}, {ItemId::Presentation, "PRESENTATION"},
            {ItemId::MasterVolume, "MASTER VOLUME"}, {ItemId::MusicVolume, "MUSIC VOLUME"},
            {ItemId::SfxVolume, "SFX VOLUME"}, {ItemId::OutputDevice, "OUTPUT DEVICE"},
            {ItemId::AccessibilityOptions, "ACCESSIBILITY OPTIONS"}, {ItemId::AudioCues, "AUDIO CUES"},
            {ItemId::TrackEdgeSound, "TRACK EDGE SOUND"}, {ItemId::TrackEdgeVolume, "TRACK EDGE VOLUME"},
            {ItemId::TrackEdgeDistance, "TRACK EDGE DISTANCE"},
            {ItemId::TurnSignal, "TURN SIGNAL"}, {ItemId::TurnSignalVolume, "TURN SIGNAL VOLUME"},
            {ItemId::TurnSignalDirection, "TURN SIGNAL DIRECTION"}, {ItemId::TurnWarningTime, "TURN WARNING TIME"},
            {ItemId::BeaconSound, "BEACON SOUND"}, {ItemId::BeaconVolume, "BEACON VOLUME"},
        };
        const auto found = labels.find(id);
        return found == labels.end() ? std::string{} : found->second;
    }
    std::string value(ItemId id) const override {
        switch (id) {
            case ItemId::Language: return "ENGLISH";
            case ItemId::Subtitles: return subtitles ? "SUBTITLES ON" : "SUBTITLES OFF";
            case ItemId::Presentation: return modern ? "MODERN" : "ACCURATE";
            case ItemId::MusicVolume: return std::to_string(music) + "%";
            case ItemId::TrackEdgeSound: return text::phrase(edge ? text::Phrase::On : text::Phrase::Off);
            case ItemId::TurnWarningTime: return text::seconds_tenths(warning);
            default: return {};
        }
    }
    bool adjust(ItemId id, int step) override {
        if (id == ItemId::Subtitles) { subtitles = !subtitles; return true; }
        if (id == ItemId::Presentation) { modern = !modern; return true; }
        if (id == ItemId::TrackEdgeSound) { edge = !edge; return true; }
        if (id == ItemId::TurnWarningTime) {
            const int next = std::clamp(warning + step * 5, 10, 40);
            if (next == warning) return false;
            warning = next;
            return true;
        }
        if (id == ItemId::MusicVolume) {
            const int next = std::clamp(music + step * 5, 0, 100);
            if (next == music) return false;
            music = next;
            return true;
        }
        return false;
    }
    std::optional<std::string> activate(ItemId id) override {
        if (id != ItemId::PerformancePreset) return std::nullopt;
        ++applied;
        return std::string("BALANCED APPLIED");
    }
    int applied = 0;
};

using Speech = std::vector<Utterance>;

void opens_on_the_root_page_with_its_heading() {
    FakeSettings settings;
    Controller menu;
    const Result result = menu.open(settings);
    assert((result.speech == Speech{{"OPTIONS", true}, {"GENERAL OPTIONS, 1 of 7", false}}));
    assert(menu.rows(settings).size() == 7U && menu.rows(settings)[0].selected);
}

void moves_clamp_and_speak_with_positions() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    Result up = menu.handle(Input::Up, settings);
    assert(up.sound == Sound::None && up.speech.empty());
    Result down = menu.handle(Input::Down, settings);
    assert(down.sound == Sound::Move);
    assert((down.speech == Speech{{"DISPLAY OPTIONS, 2 of 7", true}}));
}

void pages_open_and_return_without_repeating_the_heading() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    Result open = menu.handle(Input::Accept, settings);
    assert(open.sound == Sound::Select && menu.page() == PageId::General);
    assert((open.speech == Speech{{"GENERAL OPTIONS", true}, {"ENGLISH, 1 of 2", false}}));
    menu.handle(Input::Down, settings);
    Result toggle = menu.handle(Input::Right, settings);
    assert(toggle.changed && toggle.sound == Sound::Move);
    assert((toggle.speech == Speech{{"SUBTITLES ON, 2 of 2", true}}));
    Result back = menu.handle(Input::Back, settings);
    assert(back.sound == Sound::Back && menu.page() == PageId::Root && !back.leave);
    assert((back.speech == Speech{{"GENERAL OPTIONS, 1 of 7", true}}));
}

void modern_only_rows_follow_the_presentation() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    menu.handle(Input::Down, settings);
    menu.handle(Input::Down, settings);
    Result audio = menu.handle(Input::Accept, settings);
    // Accurate: master, music, sfx, output device, sound mode.
    assert((audio.speech == Speech{{"AUDIO OPTIONS", true}, {"MASTER VOLUME, 1 of 5", false}}));
    settings.modern = true;
    assert(menu.rows(settings).size() == 12U);
    menu.handle(Input::Down, settings);
    Result music = menu.handle(Input::Left, settings);
    assert((music.speech == Speech{{"MUSIC VOLUME, 75%, 2 of 12", true}}));
    // A value at its limit does not change, sound or speak.
    settings.music = 0;
    Result limit = menu.handle(Input::Left, settings);
    assert(!limit.changed && limit.sound == Sound::None && limit.speech.empty());
}

void game_screens_and_return_leave_the_menu() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    menu.handle(Input::Down, settings);
    menu.handle(Input::Down, settings);
    menu.handle(Input::Down, settings);
    Result save = menu.handle(Input::Accept, settings);
    assert(save.leave == ItemId::SaveOptions && save.sound == Sound::Select && save.speech.empty());
    // Back from the game's Save Options: resume on it, without the heading.
    Result resumed = menu.open(settings);
    assert((resumed.speech == Speech{{"SAVE OPTIONS, 4 of 7", true}}));
    // Arriving again from the title starts over with the heading.
    Result back = menu.handle(Input::Back, settings);
    assert(back.leave == ItemId::Return && back.sound == Sound::Back);
    assert(menu.open(settings).speech.size() == 2U);
    for (int i = 0; i < 6; ++i) menu.handle(Input::Down, settings);
    Result leave = menu.handle(Input::Accept, settings);
    assert(leave.leave == ItemId::Return && leave.sound == Sound::Back);
}

void a_applies_actions_and_is_ignored_elsewhere() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    menu.handle(Input::Down, settings);
    Result display = menu.handle(Input::Accept, settings);
    assert(menu.page() == PageId::Display && display.speech.front().text == "DISPLAY OPTIONS");
    // Presentation: A does nothing; left/right changes it.
    Result nothing = menu.handle(Input::Accept, settings);
    assert(nothing.sound == Sound::None && nothing.speech.empty() && !nothing.changed);
    menu.handle(Input::Down, settings);
    menu.handle(Input::Down, settings);
    Result applied = menu.handle(Input::Accept, settings);
    assert(settings.applied == 1 && applied.changed && applied.sound == Sound::Select);
    assert((applied.speech == Speech{{"BALANCED APPLIED", true}}));
}

void accessibility_options_hold_the_audio_cues() {
    FakeSettings settings;
    Controller menu;
    menu.open(settings);
    for (int i = 0; i < 5; ++i) menu.handle(Input::Down, settings);
    assert(menu.rows(settings)[5].text == "ACCESSIBILITY OPTIONS" && menu.rows(settings)[5].selected);
    Result accessibility = menu.handle(Input::Accept, settings);
    assert(menu.page() == PageId::AccessibilityOptions && accessibility.sound == Sound::Select);
    assert((accessibility.speech == Speech{{"ACCESSIBILITY OPTIONS", true}, {"AUDIO CUES, 1 of 1", false}}));
    Result cues = menu.handle(Input::Accept, settings);
    assert(menu.page() == PageId::AudioCues && menu.rows(settings).size() == 9U);
    assert((cues.speech == Speech{{"AUDIO CUES", true}, {"TRACK EDGE SOUND, ON, 1 of 9", false}}));
    Result off = menu.handle(Input::Left, settings);
    assert(off.changed && !settings.edge);
    assert((off.speech == Speech{{"TRACK EDGE SOUND, OFF, 1 of 9", true}}));
    for (int i = 0; i < 6; ++i) menu.handle(Input::Down, settings);
    Result longer = menu.handle(Input::Right, settings);
    assert(longer.changed && settings.warning == 25);
    assert((longer.speech == Speech{{"TURN WARNING TIME, 2.5 SECONDS, 7 of 9", true}}));
    // The warning time stops at its ends instead of wrapping.
    settings.warning = 40;
    Result limit = menu.handle(Input::Right, settings);
    assert(!limit.changed && limit.speech.empty() && settings.warning == 40);
    // The hub beacon's rows follow the turn signal's.
    Result beacon = menu.handle(Input::Down, settings);
    assert((beacon.speech == Speech{{"BEACON SOUND, 8 of 9", true}}));
    Result volume = menu.handle(Input::Down, settings);
    assert((volume.speech == Speech{{"BEACON VOLUME, 9 of 9", true}}));
    // The list ends there.
    assert(menu.handle(Input::Down, settings).speech.empty());
    menu.handle(Input::Up, settings);
    menu.handle(Input::Up, settings);
    // Back goes up one page at a time, each resuming on the item that opened it.
    Result up = menu.handle(Input::Back, settings);
    assert(menu.page() == PageId::AccessibilityOptions && !up.leave);
    assert((up.speech == Speech{{"AUDIO CUES, 1 of 1", true}}));
    Result root = menu.handle(Input::Back, settings);
    assert(menu.page() == PageId::Root && !root.leave);
    assert((root.speech == Speech{{"ACCESSIBILITY OPTIONS, 6 of 7", true}}));
}

void item_text_omits_absent_parts() {
    assert(item_text("MUSIC VOLUME", "80%") == "MUSIC VOLUME, 80%");
    assert(item_text("", "ENGLISH") == "ENGLISH");
    assert(item_text("RETURN", "") == "RETURN");
    assert(item_text("", "").empty());
}

} // namespace

int main() {
    opens_on_the_root_page_with_its_heading();
    moves_clamp_and_speak_with_positions();
    pages_open_and_return_without_repeating_the_heading();
    modern_only_rows_follow_the_presentation();
    game_screens_and_return_leave_the_menu();
    a_applies_actions_and_is_ignored_elsewhere();
    accessibility_options_hold_the_audio_cues();
    item_text_omits_absent_parts();
    std::puts("[test][options-menu] PASS");
    return 0;
}
