// Speech for the stock menus, driven by the hooks that
// scripts/accessibility_menu_policy.py composes into the recompiled game. The
// handler runs on the game thread at the hooked instruction, reads only the
// state the screen itself just drew or changed, and queues speech. The
// redesigned Options menu speaks for itself (options_menu/).
#include "game_text.hpp"
#include "guest_text.hpp"
#include "menu_events.hpp"
#include "menu_speech_policy.hpp"
#include "options_menu/options_screen.hpp"
#include "runtime_netplay.hpp"
#include "speech.hpp"
#include "symbol_lookup.hpp"

#include "recomp.h"

#include <cstdio>

namespace dkr::accessibility {
namespace {

// A NULL-terminated array of string pointers (gTitleMenuStrings), with its
// cursor variable.
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
    // The arrays reserve a terminating NULL slot; read at most 16 entries.
    for (std::uint32_t slot = 0; slot < 16U; ++slot) {
        const std::optional<std::uint32_t> pointer = guest_word(rdram, *array + slot * 4U);
        if (!pointer) {
            return std::nullopt;
        }
        if (*pointer == 0U) {
            break;
        }
        list.items.push_back(pointed_text(rdram, *pointer).value_or(std::string{}));
    }
    if (list.items.empty() || *selected >= list.items.size()) {
        return std::nullopt;
    }
    list.selected = *selected;
    return list;
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
            dkr::options_menu::screen_opened(rdram, *ctx, static_cast<std::int32_t>(*id));
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
    }
    std::fprintf(stderr, "[speech][menu] unknown event %u\n", event);
}
