#include "accessibility/guest_text.hpp"
#include "accessibility/menu_speech_policy.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace dkr::accessibility;

namespace {

MenuList title(std::size_t selected) { return {{"START", "OPTIONS"}, selected}; }

MenuList options(std::size_t selected) {
    return {{"ENGLISH", "SUBTITLES OFF", "AUDIO OPTIONS", "SAVE OPTIONS", "MAGIC CODES", "RETURN"}, selected};
}

void game_text_is_prepared_or_withheld() {
    assert(prepare_game_text("AUDIO OPTIONS") == "AUDIO OPTIONS");
    assert(prepare_game_text("  ADVENTURE   TWO ") == "ADVENTURE TWO");
    assert(!prepare_game_text(""));
    assert(!prepare_game_text("   "));
    // Drawn with a substitution the speech would not see.
    assert(!prepare_game_text("CONTROLLER PAK ~"));
    // Bytes outside printable ASCII have no verified meaning.
    assert(!prepare_game_text("CAF\xC9"));
    assert(!prepare_game_text("LINE\nBREAK"));
}

void title_menu_speaks_on_reveal_and_cursor() {
    MenuNarrator narrator;
    // The title screen has no displayed heading; nothing until the menu shows.
    assert(narrator.screen_opened(MenuId::Logos, std::nullopt, std::nullopt).empty());
    assert(narrator.screen_opened(MenuId::Title, std::nullopt, std::nullopt).empty());
    assert((narrator.title_revealed(title(0)) == std::vector<Utterance>{{"START, 1 of 2", true}}));
    assert((narrator.item_changed(title(1)) == std::vector<Utterance>{{"OPTIONS, 2 of 2", true}}));
}

void options_heading_then_item_and_returns() {
    MenuNarrator narrator;
    narrator.screen_opened(MenuId::Title, std::nullopt, std::nullopt);
    // Opening: the heading interrupts, the selected item queues after it.
    assert((narrator.screen_opened(MenuId::Options, "OPTIONS", options(0)) ==
            std::vector<Utterance>{{"OPTIONS", true}, {"ENGLISH, 1 of 6", false}}));
    assert((narrator.item_changed(options(2)) == std::vector<Utterance>{{"AUDIO OPTIONS, 3 of 6", true}}));
    // A value change speaks the item as now displayed.
    assert((narrator.item_changed({{"FRANCAIS", "SOUS-TITRES NON"}, 0}) ==
            std::vector<Utterance>{{"FRANCAIS, 1 of 2", true}}));
    // Back from its own sub-screen: the current item without the heading.
    narrator.screen_opened(MenuId::AudioOptions, std::nullopt, std::nullopt);
    assert((narrator.screen_opened(MenuId::Options, "OPTIONS", options(2)) ==
            std::vector<Utterance>{{"AUDIO OPTIONS, 3 of 6", true}}));
    // Arriving from the title again is a new opening with the heading.
    narrator.screen_opened(MenuId::Title, std::nullopt, std::nullopt);
    assert(narrator.screen_opened(MenuId::Options, "OPTIONS", options(0)).size() == 2U);
}

void unverifiable_entries_fail_closed() {
    MenuNarrator narrator;
    // An entry the pipeline withheld is never spoken but still counts.
    const MenuList list{{"START", ""}, 1};
    assert(narrator.title_revealed(list).empty());
    assert((narrator.item_changed({{"START", ""}, 0}) == std::vector<Utterance>{{"START, 1 of 2", true}}));
    // A cursor outside the list says nothing.
    assert(narrator.item_changed({{"START"}, 3}).empty());
    // Without a readable heading the item itself interrupts.
    assert((narrator.screen_opened(MenuId::Options, std::nullopt, options(5)) ==
            std::vector<Utterance>{{"RETURN, 6 of 6", true}}));
}

} // namespace

int main() {
    game_text_is_prepared_or_withheld();
    title_menu_speaks_on_reveal_and_cursor();
    options_heading_then_item_and_returns();
    unverifiable_entries_fail_closed();
    std::puts("[test][accessibility-menu-speech] PASS");
    return 0;
}
