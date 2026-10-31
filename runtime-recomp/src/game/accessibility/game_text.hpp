#pragma once

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

// The string at a guest pointer.
std::optional<std::string> pointed_text(std::uint8_t* rdram, std::uint32_t pointer);

} // namespace dkr::accessibility
