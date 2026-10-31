#pragma once

#include "recomp.h"

#include <cstdint>

namespace dkr::options_menu {

// Called from the menu_init hook (accessibility/menu_speech.cpp) whenever a
// game screen opens: gives the game back its remembered audio settings once
// it has booted, and opens the redesigned menu when Options opens.
void screen_opened(std::uint8_t* rdram, const recomp_context& context, std::int32_t menu_id);

} // namespace dkr::options_menu

// Runs one frame of the redesigned Options menu in place of the stock
// menu_options_loop (options_menu_policy.py). Returns zero, leaving the stock
// loop to run, when the native menu cannot verify what it needs.
extern "C" int dkr_options_menu_loop(std::uint8_t* rdram, recomp_context* context);
