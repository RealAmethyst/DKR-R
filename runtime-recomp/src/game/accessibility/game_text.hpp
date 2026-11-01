#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dkr::accessibility {

// Game text as drawn, prepared for speech; nullopt when the pointer chain or
// the text cannot be verified.

// An entry of the language-dependent menu text table (gMenuText), by its
// AssetMenuTextIdsEnum index.
std::optional<std::string> menu_text(std::uint8_t* rdram, std::uint32_t index);

// The string a pointer array (such as gAudioOutputStrings) holds at a slot.
std::optional<std::string> string_array_entry(std::uint8_t* rdram, const char* array, std::uint32_t slot);

// A 32-bit game variable by its symbol name; logs and returns nullopt when
// the symbol or the read cannot be verified.
std::optional<std::int32_t> symbol_s32(std::uint8_t* rdram, const char* name);

// The string at a guest pointer.
std::optional<std::string> pointed_text(std::uint8_t* rdram, std::uint32_t pointer);

// A level's name in the game's language, read as level_name (game.c) finds
// it: gLevelNames holds each level's names one after another, and the
// language (get_language, from sEepromSettings) picks which.
std::optional<std::string> level_name(std::uint8_t* rdram, std::int32_t level);

// A line of the first page of a game text (ASSET_GAME_TEXT, a text box), by
// its id and counted from 0, in the game's language, read from the ROM image
// the game loads it from (set_current_text loads a game text only to show
// it). Lines are decoded once per revision, language and id. Game thread.
std::optional<std::string> game_textbox_line(std::uint8_t* rdram, std::uint32_t id, std::size_t line);

} // namespace dkr::accessibility
