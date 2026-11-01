#include "accessibility/character_select_policy.hpp"
#include "accessibility/file_select_policy.hpp"
#include "accessibility/guest_text.hpp"
#include "accessibility/localization.hpp"
#include "accessibility/menu_speech_policy.hpp"
#include "accessibility/track_select_policy.hpp"

#include <cassert>
#include <cstdio>
#include <string_view>
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

void headed_screens_queue_the_item_after_the_heading() {
    MenuNarrator narrator;
    const MenuList modes{{"ADVENTURE", "TRACKS"}, 1};
    assert((narrator.opened("GAME SELECT", modes) ==
            std::vector<Utterance>{{"GAME SELECT", true}, {"TRACKS, 2 of 2", false}}));
    // An unverified heading is left out rather than guessed; the item interrupts.
    assert((narrator.opened("", modes) == std::vector<Utterance>{{"TRACKS, 2 of 2", true}}));
    assert((narrator.opened_message("CAUTION", std::string("Please do not change")) ==
            std::vector<Utterance>{{"CAUTION", true}, {"Please do not change", false}}));
    assert((narrator.opened_message("CAUTION", std::nullopt) == std::vector<Utterance>{{"CAUTION", true}}));
}

CharacterSelectRow row(int up, int left, int right, std::uint32_t character) {
    return {static_cast<std::int8_t>(up), static_cast<std::int8_t>(left), static_cast<std::int8_t>(right), character};
}

void character_grid_is_read_in_reading_order() {
    // The retail default layout: Krunch, Diddy, Bumper, Banjo above Conker,
    // Tiptup, Pipsy, Timber.
    const std::vector<CharacterSelectRow> eight{row(-1, -1, 1, 0), row(-1, 0, 2, 9), row(-1, 1, 3, 1),
                                                row(-1, 2, -1, 5), row(0, -1, 5, 3), row(1, 4, 6, 2),
                                                row(2, 5, 7, 7),   row(3, 6, -1, 4)};
    assert((reading_order(eight) == std::vector<std::size_t>{1, 2, 3, 4, 5, 6, 7, 8}));
    // Everyone unlocked: Drumstick (row 8) sits after Diddy, T.T. (row 9)
    // after Tiptup; T.T. has an upward neighbour (the retail down/up bug).
    const std::vector<CharacterSelectRow> ten{row(-1, -1, 1, 0), row(-1, 0, 8, 9), row(-1, 8, 3, 1),
                                              row(-1, 2, -1, 5), row(0, -1, 5, 3), row(1, 4, 9, 2),
                                              row(2, 9, 7, 7),   row(3, 6, -1, 4), row(-1, 1, 2, 6),
                                              row(8, 5, 6, 8)};
    assert((reading_order(ten) == std::vector<std::size_t>{1, 2, 4, 5, 6, 7, 9, 10, 3, 8}));
    // A table that is not two lines covering every row fails closed.
    std::vector<CharacterSelectRow> cycle = eight;
    cycle[3].right = 0;
    assert(!reading_order(cycle));
    std::vector<CharacterSelectRow> unreached = eight;
    unreached[2].right = -1;
    assert(!reading_order(unreached));
    std::vector<CharacterSelectRow> outside = eight;
    outside[1].right = 12;
    assert(!reading_order(outside));
}

CharacterSelectState character_state(int active, int ready) {
    CharacterSelectState state;
    state.total = 8;
    state.active_count = active;
    state.ready_count = ready;
    state.ok_prompt = "OK?";
    state.players[0] = {true, false, 2, "DIDDY"};
    if (active >= 2) {
        state.players[1] = {true, false, 4, "BANJO"};
    }
    return state;
}

void character_select_alone() {
    const CharacterSelectState state = character_state(1, 0);
    assert((CharacterSelectNarrator::opened("PLAYER SELECT", state) ==
            std::vector<Utterance>{{"PLAYER SELECT", true}, {"DIDDY, 2 of 8", false}}));
    assert((CharacterSelectNarrator::character(state, 0) == std::vector<Utterance>{{"DIDDY, 2 of 8", true}}));
    // Confirming alone makes everyone ready: the drawn "OK?".
    assert((CharacterSelectNarrator::confirmed(character_state(1, 1), 0) == std::vector<Utterance>{{"OK?", true}}));
    // An unverified "OK?" is not replaced by anything.
    CharacterSelectState unverified = character_state(1, 1);
    unverified.ok_prompt.clear();
    assert(CharacterSelectNarrator::confirmed(unverified, 0).empty());
    // The last player leaving returns to the title screen, which speaks.
    assert(CharacterSelectNarrator::left(character_state(0, 0), 0).empty());
}

void character_select_together() {
    const CharacterSelectState state = character_state(2, 0);
    assert((CharacterSelectNarrator::opened("PLAYER SELECT", state) ==
            std::vector<Utterance>{{"PLAYER SELECT", true},
                                   {"PLAYER 1, DIDDY, 2 of 8", false},
                                   {"PLAYER 2, BANJO, 4 of 8", false}}));
    assert((CharacterSelectNarrator::character(state, 1) ==
            std::vector<Utterance>{{"PLAYER 2, BANJO, 4 of 8", true}}));
    assert((CharacterSelectNarrator::confirmed(character_state(2, 1), 1) ==
            std::vector<Utterance>{{"PLAYER 2, BANJO, READY", true}}));
    assert((CharacterSelectNarrator::confirmed(character_state(2, 2), 1) == std::vector<Utterance>{{"OK?", true}}));
    // Player 3 leaves while the other two are ready: "OK?" appears.
    assert((CharacterSelectNarrator::left(character_state(2, 2), 2) ==
            std::vector<Utterance>{{"PLAYER 3 LEFT", true}, {"OK?", false}}));
    assert((CharacterSelectNarrator::left(character_state(2, 1), 2) ==
            std::vector<Utterance>{{"PLAYER 3 LEFT", true}}));
    // A port that is not joined, or outside the four, says nothing.
    assert(CharacterSelectNarrator::character(state, 2).empty());
    assert(CharacterSelectNarrator::character(state, 7).empty());
}

void record_times_are_split_like_the_game() {
    // 3741 frames at 60 per second: 1 minute, 2 seconds and 21 frames.
    const Timestamp time = timestamp_from_frames(3741, false);
    assert(time.minutes == 1 && time.seconds == 2 && time.hundredths == 35);
    // At 50 Hz the game scales the count by 1.2 first.
    const Timestamp pal = timestamp_from_frames(3000, true);
    assert(pal.minutes == 1 && pal.seconds == 0 && pal.hundredths == 0);
    assert(text::record_time(1, 2, 35) == "1 MINUTE 2.35 SECONDS");
    assert(text::record_time(2, 0, 5) == "2 MINUTES 0.05 SECONDS");
    assert(text::record_time(0, 20, 11) == "20.11 SECONDS");
}

std::vector<std::uint8_t> bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

void text_box_pages_are_read_or_withheld() {
    using namespace std::string_view_literals;
    assert(textbox_first_page(bytes("THE LAST PLAYER REMAINING\0WILL WIN!!\0\0\x02"sv)) ==
           "THE LAST PLAYER REMAINING WILL WIN!!");
    // A page break ends the first page.
    assert(textbox_first_page(bytes("FIRST\0\x01SECOND\0\x02"sv)) == "FIRST");
    // Drawing commands are not decoded: nothing is spoken.
    assert(!textbox_first_page(bytes("EGGS\0\x05\x01X\0\x02"sv)));
    // No end within the bytes read.
    assert(!textbox_first_page(bytes("UNFINISHED\0"sv)));
}

TrackGridState grid(std::int32_t row, std::string world, std::size_t selected) {
    return {std::move(world), {{"Ancient Lake", "Fossil Canyon", "?", "?", "?", "?"}, selected}, row};
}

void track_grid_speaks_world_then_track() {
    assert((TrackSelectNarrator::grid(grid(0, "Dino Domain", 0)) ==
            std::vector<Utterance>{{"Dino Domain", true}, {"Ancient Lake, 1 of 6", false}}));
    // A locked track is the game's "?" as drawn.
    assert((TrackSelectNarrator::grid_moved(0, grid(0, "Dino Domain", 2)) ==
            std::vector<Utterance>{{"?, 3 of 6", true}}));
    assert((TrackSelectNarrator::grid_moved(0, grid(1, "Sherbet Island", 1)) ==
            std::vector<Utterance>{{"Sherbet Island", true}, {"Fossil Canyon, 2 of 6", false}}));
}

TrackSetupState setup(std::int32_t stage) {
    TrackSetupState state;
    state.track = "Ancient Lake";
    state.stage = stage;
    state.vehicles[0] = {"CAR", 1, 1};
    state.records = "BEST TIME 1 MINUTE 2.35 SECONDS ABC, BEST LAP 20.11 SECONDS ABC";
    state.adventure = {{"ADVENTURE", "ADVENTURE TWO"}, 0};
    state.time_trial = {{"TIME TRIAL OFF", "TIME TRIAL ON"}, 0};
    state.racers_label = "NUMBER OF RACERS";
    state.racers = 4;
    state.ok_prompt = "OK?";
    return state;
}

void race_setup_alone() {
    const std::string car = "CAR, BEST TIME 1 MINUTE 2.35 SECONDS ABC, BEST LAP 20.11 SECONDS ABC, 1 of 1";
    assert((TrackSelectNarrator::setup_opened(setup(0)) ==
            std::vector<Utterance>{{"Ancient Lake", true}, {car, false}}));
    TrackSetupState hover = setup(0);
    hover.vehicles[0] = {"HOVERCRAFT", 2, 3};
    hover.records = "BEST TIME 58.00 SECONDS DKR, BEST LAP 19.00 SECONDS DKR";
    assert((TrackSelectNarrator::setup_changed(setup(0), hover) ==
            std::vector<Utterance>{
                {"HOVERCRAFT, BEST TIME 58.00 SECONDS DKR, BEST LAP 19.00 SECONDS DKR, 2 of 3", true}}));
    // Confirming moves to time trial, then to "OK?".
    TrackSetupState time_trial = setup(1);
    time_trial.confirmed[0] = true;
    assert((TrackSelectNarrator::setup_changed(setup(0), time_trial) ==
            std::vector<Utterance>{{"TIME TRIAL OFF, 1 of 2", true}}));
    TrackSetupState on = time_trial;
    on.time_trial.selected = 1;
    assert((TrackSelectNarrator::setup_changed(time_trial, on) ==
            std::vector<Utterance>{{"TIME TRIAL ON, 2 of 2", true}}));
    assert((TrackSelectNarrator::setup_changed(on, setup(2)) == std::vector<Utterance>{{"OK?", true}}));
    // Nothing changed (the race is starting): nothing to say.
    assert(TrackSelectNarrator::setup_changed(setup(2), setup(2)).empty());
    // ADVENTURE or ADVENTURE TWO first when the second adventure is unlocked.
    assert((TrackSelectNarrator::setup_opened(setup(-1)) ==
            std::vector<Utterance>{{"Ancient Lake", true}, {"ADVENTURE, 1 of 2", false}}));
    // A battle arena: its rules text follows separately, no "OK?".
    TrackSetupState battle = setup(2);
    battle.battle = true;
    battle.records.clear();
    assert((TrackSelectNarrator::setup_opened(battle) == std::vector<Utterance>{{"Ancient Lake", true}}));
}

void race_setup_together() {
    TrackSetupState two = setup(0);
    two.players = 2;
    two.racer_count = true;
    two.vehicles[1] = {"CAR", 1, 1};
    const std::vector<Utterance> opened = TrackSelectNarrator::setup_opened(two);
    assert(opened.size() == 3U && opened[1].text.rfind("PLAYER 1, CAR, BEST TIME", 0) == 0 &&
           opened[2] == (Utterance{"PLAYER 2, CAR, 1 of 1", false}));
    TrackSetupState ready = two;
    ready.confirmed[1] = true;
    assert((TrackSelectNarrator::setup_changed(two, ready) ==
            std::vector<Utterance>{{"PLAYER 2, CAR, READY", true}}));
    TrackSetupState racers = two;
    racers.stage = 2;
    assert((TrackSelectNarrator::setup_changed(ready, racers) ==
            std::vector<Utterance>{{"NUMBER OF RACERS, 4, 1 of 1", true}}));
    TrackSetupState six = racers;
    six.racers = 6;
    assert((TrackSelectNarrator::setup_changed(racers, six) ==
            std::vector<Utterance>{{"NUMBER OF RACERS, 6, 1 of 1", true}}));
}

FileSelectText file_text() { return {"GAME SELECT", "NEW", "ADVENTURE TWO", "COPY", "ERASE"}; }

std::array<SaveSlot, 3> saves() {
    return {SaveSlot{"ABC", true, false, 12}, SaveSlot{"DEF", true, true, 1}, SaveSlot{"GAME C", false, false, 0}};
}

void file_select_slots_and_prompts() {
    const FileSelectText text = file_text();
    assert(FileSelectNarrator::slot(text, saves(), 0) == "ABC, 12 BALLOONS, 1 of 3");
    assert(FileSelectNarrator::slot(text, saves(), 1) == "DEF, ADVENTURE TWO, 1 BALLOON, 2 of 3");
    assert(FileSelectNarrator::slot(text, saves(), 2) == "GAME C, NEW, 3 of 3");
    assert(FileSelectNarrator::slot(text, saves(), 3).empty());
    assert((FileSelectNarrator::opened(text, saves(), 0, 0) ==
            std::vector<Utterance>{{"GAME SELECT", true}, {"ABC, 12 BALLOONS, 1 of 3", false}}));
    assert(FileSelectNarrator::root_item(text, saves(), 1, 0) == "COPY, 1 of 2");
    assert(FileSelectNarrator::root_item(text, saves(), 2, 2) == "ERASE, 2 of 2");
    assert((FileSelectNarrator::prompt("SELECT GAME TO COPY", FileSelectNarrator::slot(text, saves(), 0)) ==
            std::vector<Utterance>{{"SELECT GAME TO COPY", true}, {"ABC, 12 BALLOONS, 1 of 3", false}}));
    // An unverified part fails closed rather than speaking half a slot.
    FileSelectText missing = text;
    missing.adventure_two.clear();
    assert(FileSelectNarrator::slot(missing, saves(), 1).empty());
}

MenuList name_keys(std::size_t selected) {
    MenuList keys;
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        keys.items.emplace_back(1, letter);
    }
    for (const char* key : {".", "?", "SP", "DEL", "OK"}) {
        keys.items.emplace_back(key);
    }
    keys.selected = selected;
    return keys;
}

void name_entry_speaks_keys_and_the_name_so_far() {
    assert((NameEntryNarrator::opened("ENTER YOUR INITIALS", name_keys(0)) ==
            std::vector<Utterance>{{"ENTER YOUR INITIALS", true}, {"A, 1 of 31", false}}));
    assert((NameEntryNarrator::cursor(name_keys(28)) == std::vector<Utterance>{{"SP, 29 of 31", true}}));
    assert((NameEntryNarrator::typed("AB", false, name_keys(1)) == std::vector<Utterance>{{"AB", true}}));
    // The third letter fills the name; the cursor jumps to OK.
    assert((NameEntryNarrator::typed("ABC", true, name_keys(2)) ==
            std::vector<Utterance>{{"ABC", true}, {"OK, 31 of 31", false}}));
    assert((NameEntryNarrator::deleted("A") == std::vector<Utterance>{{"A", true}}));
    assert(NameEntryNarrator::deleted("").empty());
}

void mod_wording() {
    assert(text::item("AUDIO OPTIONS", 3, 6) == "AUDIO OPTIONS, 3 of 6");
    assert(text::percent(80) == "80%");
    assert(text::decibels(3) == "+3 dB");
    assert(text::decibels(0) == "0 dB");
    assert(text::decibels(-2) == "-2 dB");
    assert(text::seconds_tenths(10) == "1 SECOND");
    assert(text::seconds_tenths(15) == "1.5 SECONDS");
    assert(text::seconds_tenths(20) == "2 SECONDS");
    assert(text::seconds_tenths(40) == "4 SECONDS");
    assert(text::phrase(text::Phrase::GeneralOptions) == "GENERAL OPTIONS");
    assert(text::phrase(text::Phrase::AccessibilityOptions) == "ACCESSIBILITY OPTIONS");
    assert(text::phrase(text::Phrase::TurnWarningTime) == "TURN WARNING TIME");
    assert(text::phrase(text::Phrase::BeaconVolume) == "BEACON VOLUME");
    assert(text::guiding("Dino Domain") == "GUIDING, Dino Domain");
    assert(text::arrived("TAJ") == "ARRIVED, TAJ");
    assert(text::no_route("BALLOON") == "NO ROUTE, BALLOON");
    assert(text::direction("TAJ", text::Phrase::BehindLeft) == "TAJ, BEHIND LEFT");
    assert(text::phrase(text::Phrase::GuidanceOff) == "GUIDANCE OFF");
    assert(text::character_name(0) == "KRUNCH");
    assert(text::character_name(8) == "T.T.");
    assert(text::character_name(9) == "DIDDY");
    assert(text::character_name(10).empty());
    assert(text::player(2) == "PLAYER 2");
    assert(text::player_left(3) == "PLAYER 3 LEFT");
    assert(text::balloons(1) == "1 BALLOON");
    assert(text::balloons(12) == "12 BALLOONS");
}

} // namespace

int main() {
    game_text_is_prepared_or_withheld();
    title_menu_speaks_on_reveal_and_cursor();
    unverifiable_entries_fail_closed();
    headed_screens_queue_the_item_after_the_heading();
    character_grid_is_read_in_reading_order();
    character_select_alone();
    character_select_together();
    record_times_are_split_like_the_game();
    text_box_pages_are_read_or_withheld();
    track_grid_speaks_world_then_track();
    race_setup_alone();
    race_setup_together();
    file_select_slots_and_prompts();
    name_entry_speaks_keys_and_the_name_so_far();
    mod_wording();
    std::puts("[test][accessibility-menu-speech] PASS");
    return 0;
}
