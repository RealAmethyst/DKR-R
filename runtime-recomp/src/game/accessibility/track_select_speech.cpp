// Tracks-mode track select as menu_track_select_loop, trackmenu_input,
// trackmenu_setup_render and func_80092188 hold and draw it. Text comes from
// the game's own functions (level_name, level_world_id, filename_decompress)
// called through GuestCalls, and from its menu text table.
#include "track_select_speech.hpp"

#include "game_text.hpp"
#include "guest_text.hpp"
#include "options_menu/game_calls.hpp"
#include "options_menu/guest_calls.hpp"
#include "revision_addresses.hpp"
#include "speech.hpp"
#include "symbol_lookup.hpp"
#include "track_select_policy.hpp"

#include <array>
#include <cstdio>

namespace dkr::accessibility::track_select {
namespace {

using options_menu::GameCalls;
using options_menu::GuestCalls;

// gMenuText indices (AssetMenuTextIdsEnum), the same in both revisions.
constexpr std::uint32_t kBestTimeText = 9U;
constexpr std::uint32_t kBestLapText = 10U;
constexpr std::uint32_t kCarText = 41U; // then HOVERCRAFT, PLANE
constexpr std::uint32_t kTimeTrialOnText = 65U;
constexpr std::uint32_t kTimeTrialOffText = 66U;
constexpr std::uint32_t kNumberOfRacersText = 136U;
constexpr std::uint32_t kAdventureText = 146U; // then ADVENTURE TWO

// gTrackSelectIDs is declared s16[4][6] (48 bytes), but the game also uses a
// fifth row, Future Fun Land, which runs on into gFFLUnlocked; that row has
// five columns (the cursor never reaches the sixth).
constexpr std::uint32_t kGridColumns = 6U;
constexpr std::uint32_t kGridRowBytes = kGridColumns * 2U;
constexpr std::int32_t kFutureFunLandRow = 4;
constexpr std::int32_t kTrophyColumn = 4;
constexpr std::int32_t kBattleColumn = 5;

// Settings (include/structs.h): per-course flags and, per vehicle, the record
// initials and times by track id.
constexpr std::uint32_t kCourseFlags = 0x04U;
constexpr std::uint32_t kLapInitials = 0x18U;
constexpr std::uint32_t kLapTimes = 0x24U;
constexpr std::uint32_t kCourseInitials = 0x30U;
constexpr std::uint32_t kCourseTimes = 0x3CU;
constexpr std::uint32_t kRaceCleared = 1U << 1;
constexpr std::uint32_t kInitialsLength = 3U; // MAX_INITIALS_LENGTH

constexpr std::uint32_t kVehicles = 3U; // car, hovercraft, plane
constexpr std::size_t kPorts = 4U;
// Rev A takes the hovercraft off Spaceport Alpha and the plane off Frosty
// Village with two or more players (trackmenu_setup_render, func_80092188).
constexpr std::int32_t kSpaceportAlpha = 15;
constexpr std::int32_t kFrostyVillage = 28;
constexpr std::uint32_t kHovercraft = 1U;
constexpr std::uint32_t kPlane = 2U;

struct ScreenState {
    std::int32_t row = -1;
    std::optional<TrackSetupState> setup;
};

ScreenState& screen() {
    static ScreenState value;
    return value;
}

void speak(const std::vector<Utterance>& utterances) {
    for (const Utterance& utterance : utterances) {
        speech::say(utterance.text, utterance.interrupt);
    }
}

std::string text_at(std::uint8_t* rdram, std::uint32_t pointer) {
    return pointed_text(rdram, pointer).value_or(std::string{});
}

std::string game_text(std::uint8_t* rdram, std::uint32_t index) {
    return menu_text(rdram, index).value_or(std::string{});
}

std::optional<std::uint32_t> settings(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> pointer = symbols::data("gSettingsPtr", 4U);
    const std::optional<std::uint32_t> value = pointer ? guest_word(rdram, *pointer) : std::nullopt;
    return value && *value != 0U ? value : std::nullopt;
}

// A u16 entry of one of the per-vehicle record arrays in Settings.
std::optional<std::uint16_t> record_entry(std::uint8_t* rdram, std::uint32_t settings, std::uint32_t field,
                                          std::uint32_t vehicle, std::int32_t track) {
    const std::optional<std::uint32_t> array = guest_word(rdram, settings + field + vehicle * 4U);
    const std::optional<std::int16_t> value =
        array ? guest_half(rdram, *array + static_cast<std::uint32_t>(track) * 2U) : std::nullopt;
    return value ? std::optional<std::uint16_t>(static_cast<std::uint16_t>(*value)) : std::nullopt;
}

std::string initials(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game, std::uint16_t packed) {
    const std::optional<std::uint32_t> buffer = calls.scratch(kInitialsLength + 1U);
    if (!buffer) {
        return {};
    }
    calls.call(game.filename_decompress, {packed, *buffer, kInitialsLength});
    return text_at(rdram, *buffer);
}

// "BEST TIME 1 MINUTE 2.35 SECONDS ABC, BEST LAP 20.11 SECONDS ABC".
std::string records(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game, std::uint32_t settings,
                    std::uint32_t vehicle, std::int32_t track) {
    const std::optional<std::int32_t> refresh = symbol_s32(rdram, "gVideoRefreshRate");
    if (!refresh) {
        return {};
    }
    const auto record = [&](std::uint32_t label, std::uint32_t times, std::uint32_t names) -> std::string {
        const std::string heading = game_text(rdram, label);
        const std::optional<std::uint16_t> frames = record_entry(rdram, settings, times, vehicle, track);
        const std::optional<std::uint16_t> packed = record_entry(rdram, settings, names, vehicle, track);
        if (heading.empty() || !frames || !packed) {
            return {};
        }
        const Timestamp time = timestamp_from_frames(*frames, *refresh == 50);
        const std::string holder = initials(rdram, calls, game, *packed);
        return heading + " " + text::record_time(time.minutes, time.seconds, time.hundredths) +
               (holder.empty() ? "" : " " + holder);
    };
    const std::string best_time = record(kBestTimeText, kCourseTimes, kCourseInitials);
    const std::string best_lap = record(kBestLapText, kLapTimes, kLapInitials);
    if (best_time.empty() || best_lap.empty()) {
        return {};
    }
    return best_time + ", " + best_lap;
}

std::optional<TrackGridState> read_grid(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game) {
    const std::optional<std::int32_t> x = symbol_s32(rdram, "gTrackSelectCursorX");
    const std::optional<std::int32_t> y = symbol_s32(rdram, "gTrackSelectCursorY");
    const std::optional<std::uint32_t> ids = symbols::data("gTrackSelectIDs", 4U * kGridRowBytes);
    const std::optional<std::uint32_t> future_fun_land = symbols::data("gFFLUnlocked", 2U);
    if (!x || !y || !ids || !future_fun_land) {
        std::fprintf(stderr, "[speech][track-select] grid unreadable\n");
        return std::nullopt;
    }
    if (*y < 0 || *y > kFutureFunLandRow || *x < 0 || *x >= static_cast<std::int32_t>(kGridColumns) ||
        (*y == kFutureFunLandRow && (*x >= kBattleColumn || *future_fun_land != *ids + 4U * kGridRowBytes))) {
        std::fprintf(stderr, "[speech][track-select] cursor or fifth row unexpected\n");
        return std::nullopt;
    }
    const std::optional<std::uint32_t> unknown_pointer = symbols::data("gDefaultTrackName", 4U);
    const std::optional<std::uint32_t> unknown = unknown_pointer ? guest_word(rdram, *unknown_pointer) : std::nullopt;
    TrackGridState state;
    state.row = *y;
    const std::uint32_t world = calls.call(game.level_world_id, {static_cast<std::uint32_t>(*y + 1)});
    state.world = text_at(rdram, calls.call(game.level_name, {world}));
    const std::uint32_t columns = *y == kFutureFunLandRow ? kGridColumns - 1U : kGridColumns;
    for (std::uint32_t column = 0; column < columns; ++column) {
        const std::optional<std::int16_t> id =
            guest_half(rdram, *ids + static_cast<std::uint32_t>(*y) * kGridRowBytes + column * 2U);
        if (!id) {
            return std::nullopt;
        }
        // trackmenu_render_2D: a locked track is drawn as gDefaultTrackName.
        const std::uint32_t name = *id < 0 ? unknown.value_or(0U)
                                           : calls.call(game.level_name, {static_cast<std::uint32_t>(*id)});
        state.tracks.items.push_back(text_at(rdram, name));
    }
    state.tracks.selected = static_cast<std::size_t>(*x);
    return state;
}

std::optional<TrackSetupState> read_setup(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game,
                                          std::uint32_t ok_prompt) {
    const std::optional<std::int32_t> track = symbol_s32(rdram, "gTrackIdForPreview");
    const std::optional<std::int32_t> column = symbol_s32(rdram, "gTrackSelectCursorX");
    const std::optional<std::int32_t> stage = symbol_s32(rdram, "gMenuStage");
    const std::optional<std::int32_t> players = symbol_s32(rdram, "gNumberOfActivePlayers");
    const std::optional<std::int32_t> adventure = symbol_s32(rdram, "gTracksMenuAdventureHighlightIndex");
    const std::optional<std::int32_t> time_trial = symbol_s32(rdram, "gTracksMenuTimeTrialHighlightIndex");
    const std::optional<std::int32_t> racers = symbol_s32(rdram, "gMultiplayerSelectedNumberOfRacers");
    const std::optional<std::uint32_t> vehicles = symbols::data("gPlayerSelectVehicle", kPorts);
    const std::optional<std::uint32_t> confirmed = symbols::data("gPlayerSelectConfirm", kPorts);
    const std::optional<std::uint32_t> game_settings = settings(rdram);
    if (!track || !column || !stage || !players || !adventure || !time_trial || !racers || !vehicles ||
        !confirmed || !game_settings || *track < 0 || *players < 1 || *players > static_cast<std::int32_t>(kPorts) ||
        *stage < -1 || *stage > 3) {
        std::fprintf(stderr, "[speech][track-select] setup unreadable\n");
        return std::nullopt;
    }
    TrackSetupState state;
    state.track = text_at(rdram, calls.call(game.level_name, {static_cast<std::uint32_t>(*track)}));
    state.battle = *column == kBattleColumn;
    // func_80092188 moves the battle column, and the trophy column unless it
    // asks ADVENTURE or ADVENTURE TWO, to stage 2 on its next frame.
    state.stage = state.battle || (*column == kTrophyColumn && *stage != -1) ? 2 : *stage;
    state.players = static_cast<std::size_t>(*players);
    state.racer_count = state.players == 2U && *column < kTrophyColumn;
    state.racers_label = game_text(rdram, kNumberOfRacersText);
    state.racers = 2 + 2 * *racers;
    state.adventure = {{game_text(rdram, kAdventureText), game_text(rdram, kAdventureText + 1U)},
                       static_cast<std::size_t>(*adventure != 0 ? 1 : 0)};
    // Index 0 leaves time trial off, 1 turns it on (set_time_trial_enabled).
    state.time_trial = {{game_text(rdram, kTimeTrialOffText), game_text(rdram, kTimeTrialOnText)},
                        static_cast<std::size_t>(*time_trial != 0 ? 1 : 0)};
    if (ok_prompt != 0U) {
        state.ok_prompt = text_at(rdram, ok_prompt);
    }

    std::uint32_t usable = calls.call(game.leveltable_vehicle_usable, {static_cast<std::uint32_t>(*track)});
    if (runtime::revision_addresses::selected_revision() == runtime::rom::Revision::UsV80 && state.players >= 2U) {
        if (*track == kSpaceportAlpha) usable &= ~(1U << kHovercraft);
        if (*track == kFrostyVillage) usable &= ~(1U << kPlane);
    }
    const std::optional<std::uint32_t> flags_array = guest_word(rdram, *game_settings + kCourseFlags);
    const std::optional<std::uint32_t> flags =
        flags_array ? guest_word(rdram, *flags_array + static_cast<std::uint32_t>(*track) * 4U) : std::nullopt;
    if (!flags) {
        return std::nullopt;
    }
    const bool choosable = (*flags & kRaceCleared) != 0U;
    for (std::size_t port = 0; port < state.players; ++port) {
        const std::optional<std::int8_t> vehicle = guest_byte(rdram, *vehicles + static_cast<std::uint32_t>(port));
        const std::optional<std::int8_t> ready = guest_byte(rdram, *confirmed + static_cast<std::uint32_t>(port));
        if (!vehicle || !ready) {
            return std::nullopt;
        }
        state.confirmed[port] = *ready != 0;
        if (*vehicle < 0 || static_cast<std::uint32_t>(*vehicle) >= kVehicles ||
            (usable & (1U << static_cast<std::uint32_t>(*vehicle))) == 0U) {
            continue;
        }
        VehicleChoice& choice = state.vehicles[port];
        choice.name = game_text(rdram, kCarText + static_cast<std::uint32_t>(*vehicle));
        // Before the race is won only the track's default can be chosen; the
        // others are drawn greyed out.
        for (std::uint32_t other = 0; other < kVehicles; ++other) {
            if ((usable & (1U << other)) != 0U && (choosable || other == static_cast<std::uint32_t>(*vehicle))) {
                ++choice.total;
                if (other == static_cast<std::uint32_t>(*vehicle)) {
                    choice.position = choice.total;
                }
            }
        }
    }
    const std::optional<std::int8_t> first_vehicle = guest_byte(rdram, *vehicles);
    if (*column < kTrophyColumn && first_vehicle && *first_vehicle >= 0 &&
        static_cast<std::uint32_t>(*first_vehicle) < kVehicles) {
        state.records =
            records(rdram, calls, game, *game_settings, static_cast<std::uint32_t>(*first_vehicle), *track);
    }
    return state;
}

// The battle rules text set_current_text just loaded into the text box.
std::optional<std::string> read_rules(std::uint8_t* rdram) {
    constexpr std::uint32_t kMaximum = 512U;
    const std::optional<std::uint32_t> pointer = symbols::data("D_8012A7A0", 4U);
    const std::optional<std::uint32_t> buffer = pointer ? guest_word(rdram, *pointer) : std::nullopt;
    if (!buffer || !guest_readable(*buffer, kMaximum)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t i = 0; i < kMaximum; ++i) {
        const std::optional<std::int8_t> value = guest_byte(rdram, *buffer + i);
        if (!value) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>(*value));
        if (*value == 1 || *value == 2) {
            break;
        }
    }
    return textbox_first_page(bytes);
}

} // namespace

void opened(std::uint8_t* rdram, const recomp_context& context) {
    GuestCalls calls(rdram, context);
    ScreenState& state = screen();
    state.setup.reset();
    state.row = -1;
    if (!calls.valid()) {
        return;
    }
    if (const auto grid = read_grid(rdram, calls, options_menu::game_calls())) {
        state.row = grid->row;
        speak(TrackSelectNarrator::grid(*grid));
    }
}

void event(std::uint8_t* rdram, const recomp_context& context, MenuEvent event, std::uint32_t ok_prompt) {
    GuestCalls calls(rdram, context);
    if (!calls.valid()) {
        return;
    }
    const GameCalls& game = options_menu::game_calls();
    ScreenState& state = screen();
    switch (event) {
        case MenuEvent::TrackGridMoved:
        case MenuEvent::TrackGridReturned:
            state.setup.reset();
            if (const auto grid = read_grid(rdram, calls, game)) {
                speak(event == MenuEvent::TrackGridMoved ? TrackSelectNarrator::grid_moved(state.row, *grid)
                                                         : TrackSelectNarrator::grid(*grid));
                state.row = grid->row;
            }
            return;
        case MenuEvent::TrackSetupOpened:
            state.setup = read_setup(rdram, calls, game, ok_prompt);
            if (state.setup) {
                speak(TrackSelectNarrator::setup_opened(*state.setup));
            }
            return;
        case MenuEvent::TrackSetupChanged:
            if (auto setup = read_setup(rdram, calls, game, ok_prompt)) {
                if (state.setup) {
                    speak(TrackSelectNarrator::setup_changed(*state.setup, *setup));
                }
                state.setup = std::move(setup);
            }
            return;
        case MenuEvent::TrackRulesShown:
            if (const auto rules = read_rules(rdram)) {
                speech::say(*rules, false);
            } else {
                std::fprintf(stderr, "[speech][track-select] rules text unverified\n");
            }
            return;
        default:
            return;
    }
}

} // namespace dkr::accessibility::track_select
