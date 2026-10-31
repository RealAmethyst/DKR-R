#pragma once

#include "menu_events.hpp"

#include <cstdint>
#include <span>

// Speech for File Select (MENU_FILE_SELECT) and its name entry, driven by the
// menu hooks through menu_speech.cpp.
namespace dkr::accessibility::file_select {

// value: the new carousel index for FileNameCursor. literals: the "OK?" text
// for FileConfirm; for name entry, the key characters, "SP", "DEL" and "OK".
void event(std::uint8_t* rdram, MenuEvent event, std::uint32_t value, std::span<const std::uint32_t> literals);

} // namespace dkr::accessibility::file_select
