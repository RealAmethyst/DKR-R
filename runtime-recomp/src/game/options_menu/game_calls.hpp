#pragma once

#include "game_payload.hpp"

// The game's own functions used by the redesigned Options menu, bound by name
// to each revision's recompiled code (game_calls_v77.cpp, game_calls_v80.cpp).
namespace dkr::options_menu {

struct GameCalls {
    // Drawing, as the stock menus draw.
    runtime::RecompiledEntrypoint draw_text;
    runtime::RecompiledEntrypoint set_text_font;
    runtime::RecompiledEntrypoint set_text_colour;
    runtime::RecompiledEntrypoint set_text_background_colour;
    runtime::RecompiledEntrypoint rendermode_reset;
    // Menu flow, input and sounds.
    runtime::RecompiledEntrypoint sound_play;
    runtime::RecompiledEntrypoint input_pressed;
    runtime::RecompiledEntrypoint menu_init;
    runtime::RecompiledEntrypoint optionscreen_free;
    runtime::RecompiledEntrypoint music_fade;
    runtime::RecompiledEntrypoint music_change_on;
    runtime::RecompiledEntrypoint transition_begin;
    // The game's own settings.
    runtime::RecompiledEntrypoint get_language;
    runtime::RecompiledEntrypoint set_language;
    runtime::RecompiledEntrypoint set_subtitles;
    runtime::RecompiledEntrypoint set_eeprom_settings_value;
    runtime::RecompiledEntrypoint unset_eeprom_settings_value;
    runtime::RecompiledEntrypoint set_stereo_pan_mode;
    runtime::RecompiledEntrypoint music_volume_config;
    runtime::RecompiledEntrypoint music_volume_config_set;
    runtime::RecompiledEntrypoint sndp_get_global_volume;
    runtime::RecompiledEntrypoint sndp_set_global_volume;
};

const GameCalls& game_calls_v77();
const GameCalls& game_calls_v80();
// The selected revision's table.
const GameCalls& game_calls();

} // namespace dkr::options_menu
