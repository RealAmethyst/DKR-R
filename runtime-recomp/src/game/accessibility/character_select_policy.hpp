#pragma once

#include "localization.hpp"
#include "menu_speech_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dkr::accessibility {

// One row of the game's character select table (gCharacterSelectBytes*):
// the first upward, leftward and rightward neighbours (-1 for none) and the
// character's id.
struct CharacterSelectRow {
    std::int8_t up = -1;
    std::int8_t left = -1;
    std::int8_t right = -1;
    std::uint32_t character = 0;
};

// Each row's 1-based position in reading order: the row with no upward and no
// leftward neighbour starts the top line, the other row with no leftward
// neighbour starts the bottom line, and each line follows the rightward
// neighbours. Nothing when the table is not two such lines covering every row.
inline std::optional<std::vector<std::size_t>> reading_order(const std::vector<CharacterSelectRow>& rows) {
    std::vector<std::size_t> starts;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (rows[row].left < 0) {
            starts.push_back(row);
        }
    }
    if (starts.size() != 2U) {
        return std::nullopt;
    }
    if (rows[starts[0]].up >= 0) {
        std::swap(starts[0], starts[1]);
    }
    if (rows[starts[0]].up >= 0 || rows[starts[1]].up < 0) {
        return std::nullopt;
    }
    std::vector<std::size_t> positions(rows.size(), 0);
    std::size_t next = 1;
    for (const std::size_t start : starts) {
        for (std::int32_t row = static_cast<std::int32_t>(start); row >= 0; row = rows[row].right) {
            if (static_cast<std::size_t>(row) >= rows.size() || positions[row] != 0) {
                return std::nullopt;
            }
            positions[row] = next++;
        }
    }
    if (next != rows.size() + 1U) {
        return std::nullopt;
    }
    return positions;
}

// Character select as the game holds it, by controller port. Characters are
// in the screen's reading order: the top row left to right, then the bottom
// row. A name the screen cannot verify is empty and is never spoken.
struct CharacterSelectPlayer {
    bool active = false;
    bool ready = false;
    std::size_t position = 0; // 1-based in reading order
    std::string name;
};

struct CharacterSelectState {
    std::array<CharacterSelectPlayer, 4> players;
    std::size_t total = 0;
    // The game's own counts; "OK?" is drawn when they are equal and nonzero.
    int active_count = 0;
    int ready_count = 0;
    // The drawn "OK?" prompt; empty when it could not be verified.
    std::string ok_prompt;
};

// What character select says. With two or more players joined, each
// announcement starts with the player it belongs to; alone, it does not.
class CharacterSelectNarrator {
public:
    // PLAYER SELECT, then each joined player's character.
    static std::vector<Utterance> opened(const std::string& heading, const CharacterSelectState& state) {
        std::vector<Utterance> result;
        if (!heading.empty()) {
            result.push_back({heading, true});
        }
        for (std::size_t port = 0; port < state.players.size(); ++port) {
            if (state.players[port].active) {
                append(result, item(state, port), result.empty());
            }
        }
        return result;
    }

    // A player moved, joined, or took a confirmation back: their character.
    static std::vector<Utterance> character(const CharacterSelectState& state, std::size_t port) {
        std::vector<Utterance> result;
        append(result, item(state, port), true);
        return result;
    }

    // A player confirmed: "OK?" when everyone is ready, else who is ready.
    static std::vector<Utterance> confirmed(const CharacterSelectState& state, std::size_t port) {
        std::vector<Utterance> result;
        if (all_ready(state)) {
            append(result, state.ok_prompt, true);
        } else if (port < state.players.size() && !state.players[port].name.empty()) {
            append(result, prefix(state, port) + state.players[port].name + ", " + text::phrase(text::Phrase::Ready),
                   true);
        }
        return result;
    }

    // A player left with B. The last player leaving returns to the title
    // screen, which announces itself.
    static std::vector<Utterance> left(const CharacterSelectState& state, std::size_t port) {
        std::vector<Utterance> result;
        if (state.active_count <= 0 || port >= state.players.size()) {
            return result;
        }
        result.push_back({text::player_left(port + 1U), true});
        if (all_ready(state)) {
            append(result, state.ok_prompt, false);
        }
        return result;
    }

private:
    static bool all_ready(const CharacterSelectState& state) {
        return state.active_count > 0 && state.ready_count == state.active_count;
    }

    static std::string prefix(const CharacterSelectState& state, std::size_t port) {
        return state.active_count >= 2 ? text::player(port + 1U) + ", " : std::string{};
    }

    static std::string item(const CharacterSelectState& state, std::size_t port) {
        if (port >= state.players.size()) {
            return {};
        }
        const CharacterSelectPlayer& player = state.players[port];
        if (!player.active || player.name.empty() || player.position == 0 || player.position > state.total) {
            return {};
        }
        return prefix(state, port) + text::item(player.name, player.position, state.total);
    }

    static void append(std::vector<Utterance>& result, std::string text, bool interrupt) {
        if (!text.empty()) {
            result.push_back({std::move(text), interrupt});
        }
    }
};

} // namespace dkr::accessibility
