#include "accessibility/guest_text.hpp"
#include "accessibility/localization.hpp"
#include "accessibility/menu_speech_policy.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace dkr::accessibility;

namespace {

MenuList title(std::size_t selected) { return {{"START", "OPTIONS"}, selected}; }

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
    assert((narrator.title_revealed(title(0)) == std::vector<Utterance>{{"START, 1 of 2", true}}));
    assert((narrator.item_changed(title(1)) == std::vector<Utterance>{{"OPTIONS, 2 of 2", true}}));
}

void unverifiable_entries_fail_closed() {
    MenuNarrator narrator;
    // An entry the pipeline withheld is never spoken but still counts.
    assert(narrator.title_revealed({{"START", ""}, 1}).empty());
    assert((narrator.item_changed({{"START", ""}, 0}) == std::vector<Utterance>{{"START, 1 of 2", true}}));
    // A cursor outside the list says nothing.
    assert(narrator.item_changed({{"START"}, 3}).empty());
}

void mod_wording() {
    assert(text::item("AUDIO OPTIONS", 3, 6) == "AUDIO OPTIONS, 3 of 6");
    assert(text::percent(80) == "80%");
    assert(text::decibels(3) == "+3 dB");
    assert(text::decibels(0) == "0 dB");
    assert(text::decibels(-2) == "-2 dB");
    assert(text::phrase(text::Phrase::GeneralOptions) == "GENERAL OPTIONS");
}

} // namespace

int main() {
    game_text_is_prepared_or_withheld();
    title_menu_speaks_on_reveal_and_cursor();
    unverifiable_entries_fail_closed();
    mod_wording();
    std::puts("[test][accessibility-menu-speech] PASS");
    return 0;
}
