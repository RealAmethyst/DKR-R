#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace dkr::accessibility::text {

// Mod-owned wording, kept in one place for translation. Game content always
// comes from the game's own text; only accessibility phrasing lives here.

// A list position: "3 of 6".
std::string position(std::size_t index, std::size_t total);

// A menu item spoken with its position: "AUDIO OPTIONS, 3 of 6".
std::string item(std::string_view name, std::size_t index, std::size_t total);

} // namespace dkr::accessibility::text
