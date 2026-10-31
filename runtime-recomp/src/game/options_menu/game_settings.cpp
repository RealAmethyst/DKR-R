#include "game_settings.hpp"

#include "accessibility/symbol_lookup.hpp"
#include "accessibility/display_text.hpp"
#include "accessibility/game_text.hpp"
#include "accessibility/guest_text.hpp"
#include "accessibility/localization.hpp"
#include "display_bridge.hpp"
#include "game_audio_memory.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_platform.hpp"
#include "runtime_ui.hpp"

#include "recomp.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace dkr::options_menu {
namespace {

namespace text = accessibility::text;
using text::Phrase;
namespace enh = runtime::enhancements;

// Menu text table entries (AssetMenuTextIdsEnum in the decomp's generated
// include/asset_enums.h; identical in US 1.0 and Rev A).
enum MenuText : std::uint32_t {
    kSfxVolume = 3,
    kMusicVolume = 4,
    kReturn = 5,
    kAudioOptions = 6,
    kMagicCodes = 17,
    kLanguage = 34, // the current language's name, as the stock Options shows it
    kOptions = 36,
    kSaveOptions = 165,
    kSubtitlesOn = 182,
    kSubtitlesOff = 183,
};

// Language values (Language in the decomp's include/structs.h). The US game
// offers English and French (menu_options_loop).
constexpr std::uint32_t kEnglish = 0;
constexpr std::uint32_t kFrench = 2;
// sEepromSettings bit for subtitles (menu_options_loop, set_subtitles).
constexpr std::uint32_t kSubtitlesBit = 0x02000000U;
// gAudioOutputType: STEREO, MONO, HEADPHONES.
constexpr std::uint32_t kSoundModes = 3;
// DKR-R's Sound page channels (runtime_ui.hpp).
constexpr std::size_t kMasterChannel = 0;
constexpr std::size_t kVehiclesChannel = 3;
constexpr std::size_t kNatureChannel = 4;
constexpr int kVolumeStep = 5;

gpr guest_register(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}

std::string game_text(std::uint8_t* rdram, std::uint32_t index) {
    return accessibility::menu_text(rdram, index).value_or(std::string{});
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

int percent_of(std::uint32_t value) {
    return static_cast<int>(std::lround(static_cast<double>(value) * 100.0 / 256.0));
}

std::uint32_t game_units(int percent) {
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(percent) * 256.0 / 100.0));
}

bool modern() {
    return enh::modern_options_visible(enh::presentation_profile());
}

std::optional<std::size_t> channel_of(ItemId id) {
    switch (id) {
        case ItemId::MasterVolume: return kMasterChannel;
        case ItemId::VehiclesVolume: return kVehiclesChannel;
        case ItemId::NatureVolume: return kNatureChannel;
        default: return std::nullopt;
    }
}

std::optional<std::size_t> band_of(ItemId id) {
    switch (id) {
        case ItemId::EqualiserBass: return 0U;
        case ItemId::EqualiserMid: return 1U;
        case ItemId::EqualiserTreble: return 2U;
        default: return std::nullopt;
    }
}

int wrap(int index, int step, int count) {
    return ((index + step) % count + count) % count;
}

} // namespace

GameSettings::GameSettings(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game,
                           const std::vector<std::string>& output_devices)
    : rdram_(rdram), calls_(calls), game_(game), output_devices_(output_devices) {}

bool GameSettings::visible(ItemId id) const {
    if (runtime::ui::display::handles(id)) return runtime::ui::display::visible(id);
    switch (id) {
        case ItemId::VehiclesVolume:
        case ItemId::NatureVolume:
        case ItemId::MultiplayerMusic:
        case ItemId::EqualiserPreset:
        case ItemId::EqualiserBass:
        case ItemId::EqualiserMid:
        case ItemId::EqualiserTreble:
            // DKR-R's mix and equaliser apply in Modern only, as in the launcher.
            return modern();
        default:
            return true;
    }
}

std::string GameSettings::heading(PageId page) const {
    switch (page) {
        case PageId::Root: return game_text(rdram_, kOptions);
        case PageId::General: return text::phrase(Phrase::GeneralOptions);
        case PageId::Audio: return game_text(rdram_, kAudioOptions);
        default: return text::display_heading(page);
    }
}

std::string GameSettings::label(ItemId id) const {
    if (id == ItemId::DisplayOptions || runtime::ui::display::handles(id)) return text::display_label(id);
    switch (id) {
        case ItemId::GeneralOptions: return text::phrase(Phrase::GeneralOptions);
        case ItemId::AudioOptions: return game_text(rdram_, kAudioOptions);
        case ItemId::SaveOptions: return game_text(rdram_, kSaveOptions);
        case ItemId::MagicCodes: return game_text(rdram_, kMagicCodes);
        case ItemId::Return: return game_text(rdram_, kReturn);
        case ItemId::MasterVolume: return text::phrase(Phrase::MasterVolume);
        case ItemId::MusicVolume: return game_text(rdram_, kMusicVolume);
        case ItemId::SfxVolume: return game_text(rdram_, kSfxVolume);
        case ItemId::VehiclesVolume: return text::phrase(Phrase::VehiclesVolume);
        case ItemId::NatureVolume: return text::phrase(Phrase::NatureVolume);
        case ItemId::OutputDevice: return text::phrase(Phrase::OutputDevice);
        case ItemId::MultiplayerMusic: return text::phrase(Phrase::MultiplayerMusic);
        case ItemId::EqualiserPreset: return text::phrase(Phrase::Equaliser);
        case ItemId::EqualiserBass: return text::phrase(Phrase::Bass);
        case ItemId::EqualiserMid: return text::phrase(Phrase::Mid);
        case ItemId::EqualiserTreble: return text::phrase(Phrase::Treble);
        // The game shows these by their value alone.
        case ItemId::Language:
        case ItemId::Subtitles:
        case ItemId::SoundMode:
            return {};
    }
    return {};
}

std::string GameSettings::value(ItemId id) const {
    if (runtime::ui::display::handles(id)) return runtime::ui::display::value(id);
    if (const auto channel = channel_of(id)) {
        return text::percent(static_cast<int>(std::lround(runtime::ui::sound_level(*channel) * 100.0F)));
    }
    if (const auto band = band_of(id)) {
        return text::decibels(runtime::ui::sound_band(*band));
    }
    switch (id) {
        case ItemId::Language:
            return game_text(rdram_, kLanguage);
        case ItemId::Subtitles:
            return game_text(rdram_, subtitles_on() ? kSubtitlesOn : kSubtitlesOff);
        case ItemId::MusicVolume:
            return text::percent(percent_of(game_volume(game_.music_volume_config)));
        case ItemId::SfxVolume:
            return text::percent(percent_of(game_volume(game_.sndp_get_global_volume)));
        case ItemId::OutputDevice: {
            const std::string saved = runtime::platform::audio_output_device();
            if (saved.empty()) return text::phrase(Phrase::SystemDefault);
            const bool connected =
                std::find(output_devices_.begin(), output_devices_.end(), saved) != output_devices_.end();
            return connected ? saved : saved + ", " + text::phrase(Phrase::NotConnected);
        }
        case ItemId::SoundMode: {
            const std::optional<std::uint32_t> mode = sound_mode();
            return mode ? accessibility::string_array_entry(rdram_, "gAudioOutputStrings", *mode).value_or(std::string{})
                        : std::string{};
        }
        case ItemId::MultiplayerMusic:
            return text::phrase(enh::multiplayer_race_music_requested() ? Phrase::On : Phrase::Off);
        case ItemId::EqualiserPreset: {
            const std::optional<std::size_t> preset = runtime::ui::sound_preset();
            const std::vector<std::string> labels = runtime::ui::sound_preset_labels();
            return preset && *preset < labels.size() ? upper(labels[*preset]) : text::phrase(Phrase::Custom);
        }
        default:
            return {};
    }
}

bool GameSettings::adjust(ItemId id, int step) {
    if (runtime::ui::display::handles(id)) return runtime::ui::display::adjust(id, step);
    if (const auto channel = channel_of(id)) {
        const int level = static_cast<int>(std::lround(runtime::ui::sound_level(*channel) * 100.0F));
        const int next = std::clamp(level + step * kVolumeStep, 0, 100);
        if (next == level) return false;
        runtime::ui::set_sound_level(*channel, static_cast<float>(next) / 100.0F);
        return true;
    }
    if (const auto band = band_of(id)) {
        const int level = runtime::ui::sound_band(*band);
        const int next = std::clamp(level + step, -12, 12);
        if (next == level) return false;
        runtime::ui::set_sound_band(*band, next);
        return true;
    }
    switch (id) {
        case ItemId::Language: {
            const std::uint32_t language = calls_.call(game_.get_language, {});
            calls_.call(game_.set_language, {language == kEnglish ? kFrench : kEnglish});
            return true;
        }
        case ItemId::Subtitles: {
            // As menu_options_loop: the 64-bit flag goes in a0 (high) and a1 (low).
            const bool on = subtitles_on();
            calls_.call(on ? game_.unset_eeprom_settings_value : game_.set_eeprom_settings_value, {0U, kSubtitlesBit});
            calls_.call(game_.set_subtitles, {on ? 0U : 1U});
            return true;
        }
        case ItemId::MusicVolume:
        case ItemId::SfxVolume: {
            const bool music = id == ItemId::MusicVolume;
            const int level = percent_of(game_volume(music ? game_.music_volume_config : game_.sndp_get_global_volume));
            const int next = std::clamp(level + step * kVolumeStep, 0, 100);
            if (next == level) return false;
            const std::uint32_t units = game_units(next);
            calls_.call(music ? game_.music_volume_config_set : game_.sndp_set_global_volume, {units});
            remember_game_audio(music ? GameAudio{units, std::nullopt, std::nullopt}
                                      : GameAudio{std::nullopt, units, std::nullopt});
            return true;
        }
        case ItemId::OutputDevice: {
            std::vector<std::string> choices{std::string{}};
            choices.insert(choices.end(), output_devices_.begin(), output_devices_.end());
            const std::string saved = runtime::platform::audio_output_device();
            const auto found = std::find(choices.begin(), choices.end(), saved);
            const int count = static_cast<int>(choices.size());
            const int next = found == choices.end() ? (step > 0 ? 0 : count - 1)
                                                    : wrap(static_cast<int>(found - choices.begin()), step, count);
            if (choices[static_cast<std::size_t>(next)] == saved) return false;
            runtime::platform::set_audio_output_device(choices[static_cast<std::size_t>(next)]);
            return true;
        }
        case ItemId::SoundMode: {
            const std::optional<std::uint32_t> mode = sound_mode();
            const std::optional<std::uint32_t> symbol = accessibility::symbols::data("gAudioOutputType", 4U);
            if (!mode || !symbol) return false;
            const auto next = static_cast<std::uint32_t>(wrap(static_cast<int>(*mode), step, kSoundModes));
            // As menu_audio_options_loop: store the type, then apply it.
            std::uint8_t* rdram = rdram_;
            MEM_W(0, guest_register(*symbol)) = static_cast<std::int32_t>(next);
            calls_.call(game_.set_stereo_pan_mode, {next});
            remember_game_audio({std::nullopt, std::nullopt, next});
            return true;
        }
        case ItemId::MultiplayerMusic:
            enh::set_multiplayer_race_music_enabled(!enh::multiplayer_race_music_requested());
            return true;
        case ItemId::EqualiserPreset: {
            // The presets, then the kept custom curve when there is one.
            const int presets = static_cast<int>(runtime::ui::sound_preset_labels().size());
            const std::optional<std::size_t> current = runtime::ui::sound_preset();
            const bool custom = !current || runtime::ui::sound_custom_curve_kept();
            const int count = presets + (custom ? 1 : 0);
            const int index = current ? static_cast<int>(*current) : presets;
            const int next = wrap(index, step, count);
            if (next == index) return false;
            runtime::ui::apply_sound_preset(next < presets ? std::optional<std::size_t>(static_cast<std::size_t>(next))
                                                           : std::nullopt);
            return true;
        }
        default:
            return false;
    }
}

std::optional<std::string> GameSettings::activate(ItemId id) {
    if (runtime::ui::display::handles(id)) return runtime::ui::display::activate(id);
    return std::nullopt;
}

std::uint32_t GameSettings::game_volume(runtime::RecompiledEntrypoint getter) const {
    return std::min(calls_.call(getter, {}), 256U);
}

bool GameSettings::subtitles_on() const {
    // sEepromSettings is a u64; the flag sits in its low word.
    const std::optional<std::uint32_t> symbol = accessibility::symbols::data("sEepromSettings", 8U);
    const std::optional<std::uint32_t> low = symbol ? accessibility::guest_word(rdram_, *symbol + 4U) : std::nullopt;
    return low && (*low & kSubtitlesBit) != 0U;
}

std::optional<std::uint32_t> GameSettings::sound_mode() const {
    const std::optional<std::uint32_t> symbol = accessibility::symbols::data("gAudioOutputType", 4U);
    const std::optional<std::uint32_t> mode = symbol ? accessibility::guest_word(rdram_, *symbol) : std::nullopt;
    return mode && *mode < kSoundModes ? mode : std::nullopt;
}

} // namespace dkr::options_menu
