// Speech for the boot, title and Options screens, driven by the hooks that
// scripts/accessibility_menu_policy.py composes into the recompiled game. The
// handler runs on the game thread at the hooked instruction, reads only the
// state the screen itself just drew or changed, and queues speech.
#include "game_symbols.hpp"
#include "guest_text.hpp"
#include "menu_events.hpp"
#include "menu_speech_policy.hpp"
#include "runtime_netplay.hpp"
#include "speech.hpp"

#include "recomp.h"

#include <cstdio>

namespace dkr::accessibility {
namespace {

// Speech prepared from a guest string pointer, or empty when the pointer or
// text cannot be verified.
std::string spoken_text(std::uint8_t* rdram, std::uint32_t pointer) {
    if (pointer == 0U) {
        return {};
    }
    const std::optional<std::string> raw = guest_string(rdram, pointer);
    if (!raw) {
        return {};
    }
    return prepare_game_text(*raw).value_or(std::string{});
}

// A NULL-terminated array of string pointers (gTitleMenuStrings,
// gOptionMenuStrings), with its cursor variable.
std::optional<MenuList> read_list(std::uint8_t* rdram, const char* strings, const char* cursor) {
    const std::optional<std::uint32_t> array = symbols::data(strings, 8U);
    const std::optional<std::uint32_t> index = symbols::data(cursor, 4U);
    if (!array || !index) {
        std::fprintf(stderr, "[speech][menu] symbol missing: %s or %s\n", strings, cursor);
        return std::nullopt;
    }
    const std::optional<std::uint32_t> selected = guest_word(rdram, *index);
    if (!selected) {
        return std::nullopt;
    }
    MenuList list;
    // Both arrays reserve a terminating NULL slot; read at most 16 entries.
    for (std::uint32_t slot = 0; slot < 16U; ++slot) {
        const std::optional<std::uint32_t> pointer = guest_word(rdram, *array + slot * 4U);
        if (!pointer) {
            return std::nullopt;
        }
        if (*pointer == 0U) {
            break;
        }
        list.items.push_back(spoken_text(rdram, *pointer));
    }
    if (list.items.empty() || *selected >= list.items.size()) {
        return std::nullopt;
    }
    list.selected = *selected;
    return list;
}

// One entry of the language-dependent menu text table (gMenuText).
std::optional<std::string> menu_text(std::uint8_t* rdram, MenuText entry) {
    const std::optional<std::uint32_t> table_symbol = symbols::data("gMenuText", 4U);
    if (!table_symbol) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> table = guest_word(rdram, *table_symbol);
    if (!table) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> pointer =
        guest_word(rdram, *table + static_cast<std::uint32_t>(entry) * 4U);
    if (!pointer) {
        return std::nullopt;
    }
    std::string text = spoken_text(rdram, *pointer);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

MenuNarrator& narrator() {
    static MenuNarrator value;
    return value;
}

void speak(const std::vector<Utterance>& utterances) {
    for (const Utterance& utterance : utterances) {
        speech::say(utterance.text, utterance.interrupt);
    }
}

} // namespace
} // namespace dkr::accessibility

extern "C" void dkr_accessibility_menu_event(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t event) {
    using namespace dkr::accessibility;
    if (rdram == nullptr || ctx == nullptr || !dkr::runtime::netplay::external_side_effects_allowed()) {
        return;
    }
    switch (static_cast<MenuEvent>(event)) {
        case MenuEvent::ScreenOpened: {
            const std::optional<std::uint32_t> id_symbol = symbols::data("gCurrentMenuId", 4U);
            const std::optional<std::uint32_t> id = id_symbol ? guest_word(rdram, *id_symbol) : std::nullopt;
            if (!id) {
                return;
            }
            const auto menu = static_cast<MenuId>(static_cast<std::int32_t>(*id));
            if (menu == MenuId::Options) {
                speak(narrator().screen_opened(menu, menu_text(rdram, MenuText::OptionsHeading),
                                               read_list(rdram, "gOptionMenuStrings", "gMenuCurIndex")));
            } else {
                speak(narrator().screen_opened(menu, std::nullopt, std::nullopt));
            }
            return;
        }
        case MenuEvent::TitleRevealed:
            if (const auto list = read_list(rdram, "gTitleMenuStrings", "gTitleScreenCurrentOption")) {
                speak(narrator().title_revealed(*list));
            }
            return;
        case MenuEvent::TitleCursor:
            if (const auto list = read_list(rdram, "gTitleMenuStrings", "gTitleScreenCurrentOption")) {
                speak(narrator().item_changed(*list));
            }
            return;
        case MenuEvent::OptionsCursor:
        case MenuEvent::OptionsLanguage:
        case MenuEvent::OptionsSubtitles:
            if (const auto list = read_list(rdram, "gOptionMenuStrings", "gMenuCurIndex")) {
                speak(narrator().item_changed(*list));
            }
            return;
    }
    std::fprintf(stderr, "[speech][menu] unknown event %u\n", event);
}
