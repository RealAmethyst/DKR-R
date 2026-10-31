#include "game_audio_memory.hpp"

#include <charconv>
#include <mutex>

namespace dkr::options_menu {
namespace {

std::mutex g_mutex;
GameAudio g_audio;

std::optional<std::uint32_t> parse(const std::string& value, std::uint32_t maximum) {
    std::uint32_t number = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || number > maximum) {
        return std::nullopt;
    }
    return number;
}

void write(std::ostream& output, const char* key, const std::optional<std::uint32_t>& value) {
    if (value) output << key << '=' << *value << '\n';
}

} // namespace

GameAudio remembered_game_audio() {
    std::scoped_lock lock(g_mutex);
    return g_audio;
}

void remember_game_audio(const GameAudio& audio) {
    std::scoped_lock lock(g_mutex);
    if (audio.music_volume) g_audio.music_volume = audio.music_volume;
    if (audio.sfx_volume) g_audio.sfx_volume = audio.sfx_volume;
    if (audio.sound_mode) g_audio.sound_mode = audio.sound_mode;
}

bool load_game_audio_setting(const std::string& key, const std::string& value) {
    std::scoped_lock lock(g_mutex);
    if (key == "game_music_volume") {
        g_audio.music_volume = parse(value, 256U);
    } else if (key == "game_sfx_volume") {
        g_audio.sfx_volume = parse(value, 256U);
    } else if (key == "game_sound_mode") {
        g_audio.sound_mode = parse(value, 2U);
    } else {
        return false;
    }
    return true;
}

void write_game_audio_settings(std::ostream& output) {
    std::scoped_lock lock(g_mutex);
    write(output, "game_music_volume", g_audio.music_volume);
    write(output, "game_sfx_volume", g_audio.sfx_volume);
    write(output, "game_sound_mode", g_audio.sound_mode);
}

} // namespace dkr::options_menu
