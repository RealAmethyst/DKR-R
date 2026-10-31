#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>

// The game itself forgets its MUSIC VOLUME, SFX VOLUME and sound mode on every
// start. DKR-R remembers them in its own settings file and gives them back to
// the game once it has booted. Values use the game's own units: volumes
// 0-256 (music_volume_config, sndp_get_global_volume), sound mode 0-2
// (gAudioOutputType: stereo, mono, headphones).
namespace dkr::options_menu {

struct GameAudio {
    std::optional<std::uint32_t> music_volume;
    std::optional<std::uint32_t> sfx_volume;
    std::optional<std::uint32_t> sound_mode;
};

GameAudio remembered_game_audio();
void remember_game_audio(const GameAudio& audio);

// Settings file entries; false when the key is not one of these.
bool load_game_audio_setting(const std::string& key, const std::string& value);
void write_game_audio_settings(std::ostream& output);

} // namespace dkr::options_menu
