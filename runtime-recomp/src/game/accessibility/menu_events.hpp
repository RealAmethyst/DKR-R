#pragma once

#include <cstdint>

namespace dkr::accessibility {

// Event numbers passed by the hooks that scripts/accessibility_menu_policy.py
// composes into the recompiled game; the two lists must match.
enum class MenuEvent : std::uint32_t {
    ScreenOpened = 1,
    TitleRevealed = 2,
    TitleCursor = 3,
    CharacterJoined = 4,
    CharacterMoved = 5,
    CharacterConfirmed = 6,
    CharacterUnconfirmed = 7,
    CharacterLeft = 8,
    GameSelectCursor = 9,
    TrackGridMoved = 10,
    TrackSetupOpened = 11,
    TrackSetupChanged = 12,
    TrackRulesShown = 13,
    TrackGridReturned = 14,
    FileSlotsReady = 15,
    FileRoot = 16,
    FileCopySource = 17,
    FileCopyTarget = 18,
    FileEraseSource = 19,
    FileConfirm = 20,
    FileSlotSource = 21,
    FileSlotTarget = 22,
    FileNameOpened = 23,
    FileNameTyped = 24,
    FileNameDeleted = 25,
    FileNameCursor = 26,
};

// Retail menu ids (MENU_ID in the decomp's src/menu.h).
enum class MenuId : std::int32_t {
    Title = 0,
    Logos = 1,
    CharacterSelect = 3,
    FileSelect = 6,
    MagicCodes = 10,
    MagicCodesList = 11,
    Options = 12,
    AudioOptions = 13,
    SaveOptions = 14,
    TrackSelect = 15,
    GameSelect = 19,
    Boot = 26,
    Caution = 28,
};

} // namespace dkr::accessibility
