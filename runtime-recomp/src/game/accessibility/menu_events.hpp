#pragma once

#include <cstdint>

namespace dkr::accessibility {

// Event numbers passed by the hooks that scripts/accessibility_menu_policy.py
// composes into the recompiled game; the two lists must match.
enum class MenuEvent : std::uint32_t {
    ScreenOpened = 1,
    TitleRevealed = 2,
    TitleCursor = 3,
};

// Retail menu ids (MENU_ID in the decomp's src/menu.h).
enum class MenuId : std::int32_t {
    Title = 0,
    Logos = 1,
    MagicCodes = 10,
    MagicCodesList = 11,
    Options = 12,
    AudioOptions = 13,
    SaveOptions = 14,
    Boot = 26,
};

} // namespace dkr::accessibility
