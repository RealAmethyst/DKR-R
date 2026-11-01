#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Game texts (asset section ASSET_GAME_TEXT) decoded from the bytes
// set_current_text (game_text.c) loads from the ROM, without the guest.
// Game-independent: the caller supplies the asset sections.
namespace dkr::accessibility {

// get_language's Language (enums.h): 0 English, 1 German, 2 French,
// 3 Japanese. Each language's texts follow the previous language's in the
// table, this many entries further on.
constexpr std::uint32_t kGameTextLanguages = 4;
constexpr std::uint32_t kGameTextsPerLanguage = 85;
// Each of the two text box buffers (gGameTextTableEntries, 960 bytes)
// holds the text plus the end byte set_current_text writes after it.
constexpr std::size_t kTextboxBuffer = 960;

// The bytes of game text `id` in `language`, as set_current_text finds them:
// `table` is the section ASSET_GAME_TEXT_TABLE (big-endian words, the low 24
// bits an offset into `texts`, the top byte set for a subtitle), `texts` the
// section ASSET_GAME_TEXT, and a text's size the difference to the next
// entry. nullopt for an id the game would refuse, a subtitle, or a record
// outside the section or larger than a text box buffer.
std::optional<std::span<const std::uint8_t>> game_textbox(std::span<const std::uint8_t> table,
                                                          std::span<const std::uint8_t> texts, std::uint32_t id,
                                                          std::uint32_t language);

// The lines a text box draws on its first page, raw (not prepared for
// speech), as dialogue_challenge_loop walks them: each line ends with one or
// more 0 bytes; 1 (the next page) or 2 (the end, which set_current_text
// writes after the text) after them ends the page; bytes 3 to 12 are drawing
// commands (func_800C38B4), 4 and 5 with four argument bytes, the rest with
// one. nullopt when a line or a command runs past the text.
std::optional<std::vector<std::string>> textbox_first_page_lines(std::span<const std::uint8_t> textbox);

} // namespace dkr::accessibility
