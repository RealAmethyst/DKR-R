#pragma once

#include "menu_events.hpp"

#include "recomp.h"

#include <cstdint>

// Speech for tracks-mode track select (MENU_TRACK_SELECT), driven by the menu
// hooks through menu_speech.cpp.
namespace dkr::accessibility::track_select {

// The screen opened on its grid.
void opened(std::uint8_t* rdram, const recomp_context& context);

// A track select event. ok_prompt is the address of the "OK?" literal race
// setup draws, verified by the hook composer, or zero.
void event(std::uint8_t* rdram, const recomp_context& context, MenuEvent event, std::uint32_t ok_prompt);

} // namespace dkr::accessibility::track_select
