#pragma once

#include "guest_text.hpp"
#include "localization.hpp"
#include "menu_speech_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dkr::accessibility {

// A record time as get_timestamp_from_frames (racer.c) splits it: frames at
// 60 per second, scaled by 1.2 at 50 Hz.
struct Timestamp {
    int minutes = 0;
    int seconds = 0;
    int hundredths = 0;
};

inline Timestamp timestamp_from_frames(int frames, bool fifty_hertz) {
    if (fifty_hertz) {
        frames = static_cast<int>(static_cast<double>(static_cast<float>(frames)) * 1.2);
    }
    const int minutes = frames / 3600;
    const int rest = frames - minutes * 3600;
    const int seconds = rest / 60;
    return {minutes, seconds, (rest - seconds * 60) * 100 / 60};
}

// The first page of a game_text.c text box as dialogue_challenge_loop draws
// it: each line ends with one or more 0 bytes, and a 1 or 2 after them ends
// the page. Bytes 3 to 12 are drawing commands whose arguments are not
// decoded here, so such text is not spoken.
inline std::optional<std::string> textbox_first_page(std::span<const std::uint8_t> bytes) {
    std::string result;
    std::size_t index = 0;
    while (true) {
        std::string line;
        while (index < bytes.size() && bytes[index] != 0U) {
            if (bytes[index] < 0x20U) {
                return std::nullopt;
            }
            line += static_cast<char>(bytes[index++]);
        }
        const std::optional<std::string> prepared = prepare_game_text(line);
        if (!prepared) {
            return std::nullopt;
        }
        result += (result.empty() ? "" : " ") + *prepared;
        while (index < bytes.size() && bytes[index] == 0U) {
            ++index;
        }
        if (index >= bytes.size() || (bytes[index] >= 3U && bytes[index] < 0x20U)) {
            return std::nullopt;
        }
        if (bytes[index] < 3U) {
            return result;
        }
    }
}

// Tracks-mode track select. The grid: the world name drawn above it is the
// section and the tracks of that world are the list.
struct TrackGridState {
    std::string world;
    MenuList tracks;
    std::int32_t row = 0;
};

// One player's vehicle with its position among the vehicles that player can
// choose.
struct VehicleChoice {
    std::string name;
    std::size_t position = 0;
    std::size_t total = 0;
};

// Race setup as trackmenu_setup_render draws it, at the stage the screen
// actually shows (gMenuStage after the trophy and battle columns force 2).
struct TrackSetupState {
    std::string track;
    std::int32_t stage = 0;
    // A battle arena shows its rules text in place of "OK?".
    bool battle = false;
    // Two players on a race choose NUMBER OF RACERS at stage 2.
    bool racer_count = false;
    std::size_t players = 1;
    std::array<VehicleChoice, 4> vehicles;
    std::array<bool, 4> confirmed{};
    // BEST TIME and BEST LAP for player 1's vehicle; empty when not drawn.
    std::string records;
    MenuList adventure;
    MenuList time_trial;
    std::string racers_label;
    int racers = 0;
    std::string ok_prompt;
};

class TrackSelectNarrator {
public:
    // Opening the screen or returning to the grid: the world, then the track.
    static std::vector<Utterance> grid(const TrackGridState& state) { return MenuNarrator{}.opened(state.world, state.tracks); }

    // A grid move: the track alone, or the world first when it changed.
    static std::vector<Utterance> grid_moved(std::int32_t previous_row, const TrackGridState& state) {
        return previous_row == state.row ? MenuNarrator{}.item_changed(state.tracks) : grid(state);
    }

    // Setup opening: the track name, then what the current stage shows.
    static std::vector<Utterance> setup_opened(const TrackSetupState& state) {
        std::vector<Utterance> result;
        if (!state.track.empty()) {
            result.push_back({state.track, true});
        }
        append(result, stage(state));
        return result;
    }

    // A setup change: a new stage speaks what it shows; within a stage, the
    // choice that changed.
    static std::vector<Utterance> setup_changed(const TrackSetupState& previous, const TrackSetupState& state) {
        std::vector<Utterance> result;
        if (previous.stage != state.stage) {
            append(result, stage(state));
            return result;
        }
        switch (state.stage) {
            case -1:
                if (previous.adventure.selected != state.adventure.selected) {
                    append(result, MenuNarrator{}.item_changed(state.adventure));
                }
                break;
            case 0:
                for (std::size_t port = 0; port < state.players && port < state.vehicles.size(); ++port) {
                    if (state.confirmed[port] && !previous.confirmed[port]) {
                        append(result, ready(state, port));
                    } else if (state.confirmed[port] != previous.confirmed[port] ||
                               state.vehicles[port].name != previous.vehicles[port].name) {
                        append(result, vehicle(state, port));
                    }
                }
                break;
            case 1:
                if (previous.time_trial.selected != state.time_trial.selected) {
                    append(result, MenuNarrator{}.item_changed(state.time_trial));
                }
                break;
            case 2:
                if (state.racer_count && previous.racers != state.racers) {
                    append(result, racers(state));
                }
                break;
            default:
                break;
        }
        return result;
    }

private:
    static std::string prefix(const TrackSetupState& state, std::size_t port) {
        return state.players >= 2 ? text::player(port + 1U) + ", " : std::string{};
    }

    // "CAR, BEST TIME ..., BEST LAP ..., 1 of 3"; the records belong to
    // player 1's vehicle, the only one they are drawn for.
    static std::vector<Utterance> vehicle(const TrackSetupState& state, std::size_t port) {
        const VehicleChoice& choice = state.vehicles[port];
        if (choice.name.empty() || choice.position == 0 || choice.position > choice.total) {
            return {};
        }
        std::string name = prefix(state, port) + choice.name;
        if (port == 0 && !state.records.empty()) {
            name += ", " + state.records;
        }
        return {{text::item(name, choice.position, choice.total), true}};
    }

    static std::vector<Utterance> ready(const TrackSetupState& state, std::size_t port) {
        if (state.players < 2 || state.vehicles[port].name.empty()) {
            return {};
        }
        return {{prefix(state, port) + state.vehicles[port].name + ", " + text::phrase(text::Phrase::Ready), true}};
    }

    static std::vector<Utterance> racers(const TrackSetupState& state) {
        if (state.racers_label.empty() || state.racers <= 0) {
            return {};
        }
        return {{text::item(state.racers_label + ", " + std::to_string(state.racers), 1, 1), true}};
    }

    static std::vector<Utterance> stage(const TrackSetupState& state) {
        std::vector<Utterance> result;
        switch (state.stage) {
            case -1:
                append(result, MenuNarrator{}.item_changed(state.adventure));
                break;
            case 0:
                for (std::size_t port = 0; port < state.players && port < state.vehicles.size(); ++port) {
                    if (!state.confirmed[port]) {
                        append(result, vehicle(state, port));
                    }
                }
                break;
            case 1:
                append(result, MenuNarrator{}.item_changed(state.time_trial));
                break;
            case 2:
                if (state.battle) {
                    break;
                }
                if (state.racer_count) {
                    append(result, racers(state));
                    break;
                }
                [[fallthrough]];
            case 3:
                if (!state.ok_prompt.empty()) {
                    result.push_back({state.ok_prompt, true});
                }
                break;
            default:
                break;
        }
        return result;
    }

    // Adds speech, keeping only the first of the combined utterances as an
    // interruption.
    static void append(std::vector<Utterance>& result, std::vector<Utterance> more) {
        for (Utterance& utterance : more) {
            utterance.interrupt = result.empty();
            result.push_back(std::move(utterance));
        }
    }
};

} // namespace dkr::accessibility
