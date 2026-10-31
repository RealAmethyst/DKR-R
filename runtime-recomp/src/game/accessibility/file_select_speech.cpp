// File Select as fileselect_render and the fileselect_input_* functions hold
// it, and name entry as filename_render and filename_enter do. Each event
// names what the screen shows next; the state it reads is already written at
// the hooked instruction.
#include "file_select_speech.hpp"

#include "file_select_policy.hpp"
#include "game_text.hpp"
#include "guest_text.hpp"
#include "revision_addresses.hpp"
#include "speech.hpp"
#include "symbol_lookup.hpp"

#include <cstdio>

namespace dkr::accessibility::file_select {
namespace {

// gMenuText indices (AssetMenuTextIdsEnum). fileselect_render draws its
// heading from index 76 in US 1.0 and 188 in Rev A; both read GAME SELECT.
constexpr std::uint32_t kHeadingTextV77 = 76U;
constexpr std::uint32_t kHeadingTextV80 = 188U;
constexpr std::uint32_t kNewText = 75U;
constexpr std::uint32_t kCopySourceText = 77U;
constexpr std::uint32_t kCopyTargetText = 78U;
constexpr std::uint32_t kEraseSourceText = 79U;
constexpr std::uint32_t kCopyText = 80U;
constexpr std::uint32_t kEraseText = 81U;
constexpr std::uint32_t kAdventureTwoText = 143U;
constexpr std::uint32_t kEnterInitialsText = 8U;

// SavefileInfo (src/menu.h): 12 bytes, isAdventure2 u8 at 0, isStarted u8 at
// 1, balloonCount u16 at 2, name[4] at 4.
constexpr std::uint32_t kSlots = 3U;
constexpr std::uint32_t kSlotSize = 12U;

// The name carousel: 28 characters from gFileNameValidChars, then SP, DEL
// and OK.
constexpr std::uint32_t kNameCharacters = 28U;

void speak(const std::vector<Utterance>& utterances) {
    for (const Utterance& utterance : utterances) {
        speech::say(utterance.text, utterance.interrupt);
    }
}

std::string game_text(std::uint8_t* rdram, std::uint32_t index) {
    return menu_text(rdram, index).value_or(std::string{});
}

FileSelectText screen_text(std::uint8_t* rdram) {
    const bool rev_a = runtime::revision_addresses::selected_revision() == runtime::rom::Revision::UsV80;
    return {game_text(rdram, rev_a ? kHeadingTextV80 : kHeadingTextV77), game_text(rdram, kNewText),
            game_text(rdram, kAdventureTwoText), game_text(rdram, kCopyText), game_text(rdram, kEraseText)};
}

std::optional<std::array<SaveSlot, 3>> read_slots(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> info = symbols::data("gSavefileInfo", kSlots * kSlotSize);
    const std::optional<std::uint32_t> names = symbols::data("gFilenames", kSlots * 4U);
    if (!info || !names) {
        std::fprintf(stderr, "[speech][file-select] save slots unreadable\n");
        return std::nullopt;
    }
    std::array<SaveSlot, 3> slots;
    for (std::uint32_t index = 0; index < kSlots; ++index) {
        const std::uint32_t at = *info + index * kSlotSize;
        const std::optional<std::int8_t> adventure_two = guest_byte(rdram, at);
        const std::optional<std::int8_t> started = guest_byte(rdram, at + 1U);
        const std::optional<std::int16_t> balloons = guest_half(rdram, at + 2U);
        if (!adventure_two || !started || !balloons) {
            return std::nullopt;
        }
        SaveSlot& slot = slots[index];
        slot.started = *started != 0;
        slot.adventure_two = *adventure_two != 0;
        slot.balloons = static_cast<std::uint16_t>(*balloons);
        if (slot.started) {
            // filename_trim of the three initials.
            const std::optional<std::string> raw = guest_string(rdram, at + 4U, 3U);
            slot.name = raw ? prepare_game_text(*raw).value_or(std::string{}) : std::string{};
        } else {
            const std::optional<std::uint32_t> pointer = guest_word(rdram, *names + index * 4U);
            slot.name = pointer ? pointed_text(rdram, *pointer).value_or(std::string{}) : std::string{};
        }
    }
    return slots;
}

std::optional<std::size_t> slot_index(std::uint8_t* rdram, const char* name) {
    const std::optional<std::int32_t> index = symbol_s32(rdram, name);
    if (!index || *index < 0 || static_cast<std::uint32_t>(*index) >= kSlots) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*index);
}

std::string slot_text(std::uint8_t* rdram, const FileSelectText& text, const char* index_name) {
    const auto slots = read_slots(rdram);
    const auto index = slot_index(rdram, index_name);
    return slots && index ? FileSelectNarrator::slot(text, *slots, *index) : std::string{};
}

std::vector<Utterance> root(std::uint8_t* rdram, bool with_heading) {
    const FileSelectText text = screen_text(rdram);
    const auto slots = read_slots(rdram);
    const auto stage = symbol_s32(rdram, "gMenuStage");
    const auto index = slot_index(rdram, "gSaveFileIndex");
    if (!slots || !stage || !index) {
        return {};
    }
    return with_heading ? FileSelectNarrator::opened(text, *slots, *stage, *index)
                        : FileSelectNarrator::say(FileSelectNarrator::root_item(text, *slots, *stage, *index));
}

// The carousel keys as drawn; nothing when the literals are not all there.
std::optional<MenuList> name_keys(std::uint8_t* rdram, std::span<const std::uint32_t> literals) {
    if (literals.size() != 4U) {
        return std::nullopt;
    }
    MenuList keys;
    for (std::uint32_t index = 0; index < kNameCharacters; ++index) {
        const std::optional<std::int8_t> character = guest_byte(rdram, literals[0] + index);
        keys.items.push_back(character ? prepare_game_text(std::string(1, static_cast<char>(*character)))
                                             .value_or(std::string{})
                                       : std::string{});
    }
    for (std::size_t literal = 1; literal < literals.size(); ++literal) {
        keys.items.push_back(pointed_text(rdram, literals[literal]).value_or(std::string{}));
    }
    return keys;
}

// Name entry belongs to File Select when it moves gIndexOfCurInputCharacter;
// the post-race initials entry shares filename_enter with another cursor.
bool file_select_name_entry(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> target_symbol = symbols::data("gNameEntryTargetX", 4U);
    const std::optional<std::uint32_t> target = target_symbol ? guest_word(rdram, *target_symbol) : std::nullopt;
    const std::optional<std::uint32_t> cursor = symbols::data("gIndexOfCurInputCharacter", 4U);
    return target && cursor && *target == *cursor;
}

std::string name_so_far(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> pointer_symbol = symbols::data("gNameEntryString", 4U);
    const std::optional<std::uint32_t> pointer = pointer_symbol ? guest_word(rdram, *pointer_symbol) : std::nullopt;
    const std::optional<std::string> raw = pointer ? guest_string(rdram, *pointer, 8U) : std::nullopt;
    if (!raw) {
        return {};
    }
    return prepare_game_text(*raw).value_or(std::string{});
}

void name_event(std::uint8_t* rdram, MenuEvent event, std::uint32_t value, std::span<const std::uint32_t> literals) {
    if (!file_select_name_entry(rdram)) {
        return;
    }
    std::optional<MenuList> keys = name_keys(rdram, literals);
    const std::optional<std::int32_t> cursor = symbol_s32(rdram, "gIndexOfCurInputCharacter");
    if (!keys || !cursor) {
        std::fprintf(stderr, "[speech][name-entry] keys unverified\n");
        return;
    }
    const std::uint32_t selected = event == MenuEvent::FileNameCursor ? value : static_cast<std::uint32_t>(*cursor);
    if (selected >= kNameKeys) {
        return;
    }
    keys->selected = selected;
    switch (event) {
        case MenuEvent::FileNameOpened:
            speak(NameEntryNarrator::opened(game_text(rdram, kEnterInitialsText), *keys));
            return;
        case MenuEvent::FileNameCursor:
            speak(NameEntryNarrator::cursor(*keys));
            return;
        case MenuEvent::FileNameTyped: {
            const std::optional<std::int32_t> length = symbol_s32(rdram, "gNameEntryLength");
            const std::optional<std::int32_t> allowed = symbol_s32(rdram, "gNameEntryAllowedLength");
            if (length && allowed) {
                speak(NameEntryNarrator::typed(name_so_far(rdram), *length >= *allowed, *keys));
            }
            return;
        }
        case MenuEvent::FileNameDeleted:
            speak(NameEntryNarrator::deleted(name_so_far(rdram)));
            return;
        default:
            return;
    }
}

} // namespace

void event(std::uint8_t* rdram, MenuEvent event, std::uint32_t value, std::span<const std::uint32_t> literals) {
    const FileSelectText text = screen_text(rdram);
    switch (event) {
        case MenuEvent::FileSlotsReady:
            speak(root(rdram, true));
            return;
        case MenuEvent::FileRoot:
            speak(root(rdram, false));
            return;
        case MenuEvent::FileCopySource:
            speak(FileSelectNarrator::prompt(game_text(rdram, kCopySourceText),
                                             slot_text(rdram, text, "gSaveFileIndex3")));
            return;
        case MenuEvent::FileCopyTarget:
            speak(FileSelectNarrator::prompt(game_text(rdram, kCopyTargetText),
                                             slot_text(rdram, text, "gSaveFileIndex2")));
            return;
        case MenuEvent::FileEraseSource:
            speak(FileSelectNarrator::prompt(game_text(rdram, kEraseSourceText),
                                             slot_text(rdram, text, "gSaveFileIndex3")));
            return;
        case MenuEvent::FileConfirm:
            if (!literals.empty()) {
                speak(FileSelectNarrator::say(pointed_text(rdram, literals[0]).value_or(std::string{})));
            }
            return;
        case MenuEvent::FileSlotSource:
            speak(FileSelectNarrator::say(slot_text(rdram, text, "gSaveFileIndex3")));
            return;
        case MenuEvent::FileSlotTarget:
            speak(FileSelectNarrator::say(slot_text(rdram, text, "gSaveFileIndex2")));
            return;
        case MenuEvent::FileNameOpened:
        case MenuEvent::FileNameTyped:
        case MenuEvent::FileNameDeleted:
        case MenuEvent::FileNameCursor:
            name_event(rdram, event, value, literals);
            return;
        default:
            return;
    }
}

} // namespace dkr::accessibility::file_select
