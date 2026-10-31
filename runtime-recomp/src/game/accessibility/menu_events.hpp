#pragma once

#include <cstdint>

namespace dkr::accessibility {

// Event numbers passed by the hooks that scripts/accessibility_menu_policy.py
// composes into the recompiled game; the two lists must match.
enum class MenuEvent : std::uint32_t {
    ScreenOpened = 1,
    TitleRevealed = 2,
    TitleCursor = 3,
    OptionsCursor = 4,
    OptionsLanguage = 5,
    OptionsSubtitles = 6,
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

// Menu text table entries drawn directly by a screen (AssetMenuTextIdsEnum in
// the decomp's generated include/asset_enums.h; identical in US 1.0 and Rev A).
enum class MenuText : std::uint32_t {
    OptionsHeading = 36, // ASSET_MENU_TEXT_OPTIONS, optionscreen_render
};

} // namespace dkr::accessibility
