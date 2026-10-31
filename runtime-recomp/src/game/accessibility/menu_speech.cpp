// Speech for the stock menus, driven by the hooks that
// scripts/accessibility_menu_policy.py composes into the recompiled game. The
// handler runs on the game thread at the hooked instruction, reads only the
// state the screen itself just drew or changed, and queues speech. The
// redesigned Options menu speaks for itself (options_menu/).
#include "character_select_policy.hpp"
#include "file_select_speech.hpp"
#include "game_text.hpp"
#include "guest_text.hpp"
#include "menu_events.hpp"
#include "menu_speech_policy.hpp"
#include "options_menu/options_screen.hpp"
#include "runtime_netplay.hpp"
#include "speech.hpp"
#include "symbol_lookup.hpp"
#include "track_select_speech.hpp"

#include "recomp.h"

#include <array>
#include <cstdio>
#include <span>

namespace dkr::accessibility {
namespace {

// MenuElement (include/structs.h): 0x20 bytes, elementType at 0x13 (0 is
// text), the text pointer at 0x14.
constexpr std::uint32_t kElementSize = 0x20U;
constexpr std::uint32_t kElementType = 0x13U;
constexpr std::uint32_t kElementText = 0x14U;
constexpr std::int8_t kTextElement = 0;

// gMenuText index of PLAYER SELECT (ASSET_MENU_TEXT_PLAYERSELECT), the heading
// charselect_render_text draws; the same in both revisions.
constexpr std::uint32_t kPlayerSelectText = 135U;

// The character select tables (gCharacterSelectBytes*) with their row counts;
// gCurrCharacterSelectData points at one. Rows are 14 bytes: up s8[2] at 0,
// down s8[2] at 2, leftward s8[4] at 4, rightward s8[4] at 8 (the decomp
// names the last two the other way round) and the Character id s16 at 12.
struct CharacterTable {
    const char* symbol;
    std::uint32_t rows;
};
constexpr std::array kCharacterTables{
    CharacterTable{"gCharacterSelectBytesDefault", 8U},
    CharacterTable{"gCharacterSelectBytesDrumStick", 9U},
    CharacterTable{"gCharacterSelectBytesTT", 9U},
    CharacterTable{"gCharacterSelectBytesComplete", 10U},
};
constexpr std::uint32_t kCharacterRowSize = 14U;
constexpr std::uint32_t kPorts = 4U;

// The text of a MenuElement that draws text.
std::optional<std::string> element_text(std::uint8_t* rdram, std::uint32_t elements, std::uint32_t index) {
    const std::uint32_t element = elements + index * kElementSize;
    const std::optional<std::int8_t> type = guest_byte(rdram, element + kElementType);
    if (!type || *type != kTextElement) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> pointer = guest_word(rdram, element + kElementText);
    return pointer ? pointed_text(rdram, *pointer) : std::nullopt;
}

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

struct HeadedList {
    std::string heading;
    MenuList list;
};

// GAME SELECT as gameselect_render draws it: gGameSelectElements points at
// one of two element arrays; elements 0 and 1 are the heading (shadow and
// text), item i is element i*2+3, and gMenuStage is the last item's index.
std::optional<HeadedList> read_game_select(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> pointer_symbol = symbols::data("gGameSelectElements", 4U);
    const std::optional<std::uint32_t> elements = pointer_symbol ? guest_word(rdram, *pointer_symbol) : std::nullopt;
    const std::optional<std::uint32_t> two_items = symbols::data("gGameSelectTextElemsNoAdv2", 1U);
    const std::optional<std::uint32_t> three_items = symbols::data("gGameSelectTextElemsWithAdv2", 1U);
    const std::optional<std::int32_t> last = symbol_s32(rdram, "gMenuStage");
    const std::optional<std::int32_t> cursor = symbol_s32(rdram, "gMenuCurIndex");
    if (!elements || !two_items || !three_items || !last || !cursor) {
        std::fprintf(stderr, "[speech][game-select] state unreadable\n");
        return std::nullopt;
    }
    const std::int32_t expected_last = *elements == *two_items ? 1 : *elements == *three_items ? 2 : -1;
    if (expected_last < 0 || *last != expected_last || *cursor < 0 || *cursor > *last) {
        std::fprintf(stderr, "[speech][game-select] unexpected layout or cursor\n");
        return std::nullopt;
    }
    HeadedList result;
    result.heading = element_text(rdram, *elements, 1U).value_or(std::string{});
    for (std::int32_t item = 0; item <= *last; ++item) {
        result.list.items.push_back(
            element_text(rdram, *elements, static_cast<std::uint32_t>(item) * 2U + 3U).value_or(std::string{}));
    }
    result.list.selected = static_cast<std::size_t>(*cursor);
    return result;
}

// CAUTION as menu_caution_loop draws gCautionMenuTextElements: elements 0 and
// 1 are the heading, 2 to 12 the message lines.
std::vector<Utterance> caution_opened(std::uint8_t* rdram, MenuNarrator& narrator) {
    constexpr std::uint32_t kFirstLine = 2U;
    constexpr std::uint32_t kLastLine = 12U;
    const std::optional<std::uint32_t> elements =
        symbols::data("gCautionMenuTextElements", kLastLine * kElementSize + kElementText + 4U);
    if (!elements) {
        std::fprintf(stderr, "[speech][caution] symbol missing\n");
        return {};
    }
    std::optional<std::string> body = std::string{};
    for (std::uint32_t line = kFirstLine; line <= kLastLine && body; ++line) {
        const std::optional<std::string> text = element_text(rdram, *elements, line);
        if (!text) {
            body.reset();
        } else {
            *body += (body->empty() ? "" : " ") + *text;
        }
    }
    if (!body) {
        std::fprintf(stderr, "[speech][caution] message unverified\n");
    }
    return narrator.opened_message(element_text(rdram, *elements, 1U).value_or(std::string{}), std::move(body));
}

// Character select as charselect_render_text and the cursor code hold it.
// ok_prompt is the address of the "OK?" literal the screen draws, verified by
// the hook composer, or zero where the event does not need it.
std::optional<CharacterSelectState> read_character_select(std::uint8_t* rdram, std::uint32_t ok_prompt) {
    const std::optional<std::uint32_t> current_symbol = symbols::data("gCurrCharacterSelectData", 4U);
    const std::optional<std::uint32_t> current = current_symbol ? guest_word(rdram, *current_symbol) : std::nullopt;
    if (!current) {
        std::fprintf(stderr, "[speech][character-select] table pointer unreadable\n");
        return std::nullopt;
    }
    std::uint32_t row_count = 0;
    for (const CharacterTable& table : kCharacterTables) {
        if (symbols::data(table.symbol, table.rows * kCharacterRowSize) == current) {
            row_count = table.rows;
        }
    }
    if (row_count == 0U) {
        std::fprintf(stderr, "[speech][character-select] unknown table\n");
        return std::nullopt;
    }
    std::vector<CharacterSelectRow> rows;
    for (std::uint32_t row = 0; row < row_count; ++row) {
        const std::uint32_t at = *current + row * kCharacterRowSize;
        const auto up = guest_byte(rdram, at);
        const auto left = guest_byte(rdram, at + 4U);
        const auto right = guest_byte(rdram, at + 8U);
        const auto character = guest_half(rdram, at + 12U);
        if (!up || !left || !right || !character || *character < 0) {
            std::fprintf(stderr, "[speech][character-select] table row unreadable\n");
            return std::nullopt;
        }
        rows.push_back({*up, *left, *right, static_cast<std::uint32_t>(*character)});
    }
    const std::optional<std::vector<std::size_t>> positions = reading_order(rows);
    if (!positions) {
        std::fprintf(stderr, "[speech][character-select] table is not two rows\n");
        return std::nullopt;
    }
    const std::optional<std::uint32_t> active = symbols::data("gActivePlayersArray", kPorts);
    const std::optional<std::uint32_t> cursors = symbols::data("gPlayersCharacterArray", kPorts);
    const std::optional<std::uint32_t> status = symbols::data("gCharselectStatus", kPorts);
    const std::optional<std::int32_t> active_count = symbol_s32(rdram, "gNumberOfActivePlayers");
    const std::optional<std::int32_t> ready_count = symbol_s32(rdram, "gNumberOfReadyPlayers");
    if (!active || !cursors || !status || !active_count || !ready_count) {
        std::fprintf(stderr, "[speech][character-select] player state unreadable\n");
        return std::nullopt;
    }
    CharacterSelectState state;
    state.total = rows.size();
    state.active_count = *active_count;
    state.ready_count = *ready_count;
    for (std::uint32_t port = 0; port < kPorts; ++port) {
        const auto joined = guest_byte(rdram, *active + port);
        const auto row = guest_byte(rdram, *cursors + port);
        const auto confirmed = guest_byte(rdram, *status + port);
        if (!joined || !row || !confirmed) {
            return std::nullopt;
        }
        CharacterSelectPlayer& player = state.players[port];
        player.active = *joined != 0;
        player.ready = *confirmed != 0;
        if (player.active && *row >= 0 && static_cast<std::size_t>(*row) < rows.size()) {
            player.position = (*positions)[static_cast<std::size_t>(*row)];
            player.name = text::character_name(rows[static_cast<std::size_t>(*row)].character);
        }
    }
    if (ok_prompt != 0U) {
        state.ok_prompt = pointed_text(rdram, ok_prompt).value_or(std::string{});
    }
    return state;
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

void screen_opened(std::uint8_t* rdram, const recomp_context& ctx, std::int32_t id) {
    dkr::options_menu::screen_opened(rdram, ctx, id);
    switch (static_cast<MenuId>(id)) {
        case MenuId::CharacterSelect:
            if (const auto state = read_character_select(rdram, 0U)) {
                speak(CharacterSelectNarrator::opened(menu_text(rdram, kPlayerSelectText).value_or(std::string{}),
                                                      *state));
            }
            return;
        case MenuId::GameSelect:
            if (const auto screen = read_game_select(rdram)) {
                speak(narrator().opened(screen->heading, screen->list));
            }
            return;
        case MenuId::Caution:
            speak(caution_opened(rdram, narrator()));
            return;
        case MenuId::TrackSelect:
            track_select::opened(rdram, ctx);
            return;
        default:
            return;
    }
}

void character_event(std::uint8_t* rdram, MenuEvent event, std::uint32_t port, std::uint32_t ok_prompt) {
    if (port >= kPorts) {
        std::fprintf(stderr, "[speech][character-select] bad controller %u\n", port);
        return;
    }
    const std::optional<CharacterSelectState> state = read_character_select(rdram, ok_prompt);
    if (!state) {
        return;
    }
    switch (event) {
        case MenuEvent::CharacterJoined:
        case MenuEvent::CharacterMoved:
        case MenuEvent::CharacterUnconfirmed:
            speak(CharacterSelectNarrator::character(*state, port));
            return;
        case MenuEvent::CharacterConfirmed:
            speak(CharacterSelectNarrator::confirmed(*state, port));
            return;
        case MenuEvent::CharacterLeft:
            speak(CharacterSelectNarrator::left(*state, port));
            return;
        default:
            return;
    }
}

} // namespace
} // namespace dkr::accessibility

// event: MenuEvent; value: a register the site passes, such as the
// controller the event belongs to; literals: the addresses of verified text
// literals the screen draws, count of them.
extern "C" void dkr_accessibility_menu_event(std::uint8_t* rdram, recomp_context* ctx, std::uint32_t event,
                                             std::uint32_t value, const std::uint32_t* literals,
                                             std::uint32_t count) {
    using namespace dkr::accessibility;
    if (rdram == nullptr || ctx == nullptr || (literals == nullptr && count != 0U) ||
        !dkr::runtime::netplay::external_side_effects_allowed()) {
        return;
    }
    const std::span<const std::uint32_t> texts(literals, count);
    const std::uint32_t first_text = texts.empty() ? 0U : texts.front();
    switch (static_cast<MenuEvent>(event)) {
        case MenuEvent::ScreenOpened:
            if (const auto id = symbol_s32(rdram, "gCurrentMenuId")) {
                screen_opened(rdram, *ctx, *id);
            }
            return;
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
        case MenuEvent::CharacterJoined:
        case MenuEvent::CharacterMoved:
        case MenuEvent::CharacterConfirmed:
        case MenuEvent::CharacterUnconfirmed:
        case MenuEvent::CharacterLeft:
            character_event(rdram, static_cast<MenuEvent>(event), value, first_text);
            return;
        case MenuEvent::GameSelectCursor:
            if (const auto screen = read_game_select(rdram)) {
                speak(narrator().item_changed(screen->list));
            }
            return;
        case MenuEvent::TrackGridMoved:
        case MenuEvent::TrackSetupOpened:
        case MenuEvent::TrackSetupChanged:
        case MenuEvent::TrackRulesShown:
        case MenuEvent::TrackGridReturned:
            track_select::event(rdram, *ctx, static_cast<MenuEvent>(event), first_text);
            return;
        case MenuEvent::FileSlotsReady:
        case MenuEvent::FileRoot:
        case MenuEvent::FileCopySource:
        case MenuEvent::FileCopyTarget:
        case MenuEvent::FileEraseSource:
        case MenuEvent::FileConfirm:
        case MenuEvent::FileSlotSource:
        case MenuEvent::FileSlotTarget:
        case MenuEvent::FileNameOpened:
        case MenuEvent::FileNameTyped:
        case MenuEvent::FileNameDeleted:
        case MenuEvent::FileNameCursor:
            file_select::event(rdram, static_cast<MenuEvent>(event), value, texts);
            return;
    }
    std::fprintf(stderr, "[speech][menu] unknown event %u\n", event);
}
