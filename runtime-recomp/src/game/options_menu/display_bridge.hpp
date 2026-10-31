#pragma once

#include "options_menu.hpp"

#include <optional>
#include <string>

// DKR-R's graphics settings for the redesigned Options menu: DISPLAY OPTIONS
// and its sub-pages. Implemented in options_menu/display_bridge.inl, which is
// included into runtime_ui.cpp so it reuses the launcher's own settings,
// helpers and SaveSettings rather than copying them. Wording comes from
// accessibility/display_text.hpp. Called on the game thread.
namespace dkr::runtime::ui::display {

// True for the DISPLAY OPTIONS items this bridge owns.
bool handles(options_menu::ItemId id);

// False when the row is hidden: Modern-only rows in Accurate, rows another
// setting disables (as the launcher disables them), rows a platform lacks.
bool visible(options_menu::ItemId id);

// The current value as drawn and spoken (empty for page links and actions).
// A change that only applies from the next game says so.
std::string value(options_menu::ItemId id);

// One step left (-1) or right (+1). Applies and saves the change; false when
// nothing changed (at a limit, or not adjustable).
bool adjust(options_menu::ItemId id, int step);

// A on the row: applies the chosen performance preset, or runs a reset.
// Returns what to say when it did something; nullopt otherwise.
std::optional<std::string> activate(options_menu::ItemId id);

} // namespace dkr::runtime::ui::display
