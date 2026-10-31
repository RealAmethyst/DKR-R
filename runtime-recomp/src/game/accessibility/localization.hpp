#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dkr::accessibility::text {

// Mod-owned wording, kept in one place for translation. Game content always
// comes from the game's own text; only accessibility phrasing lives here.

// A list position: "3 of 6".
std::string position(std::size_t index, std::size_t total);

// A menu item spoken with its position: "AUDIO OPTIONS, 3 of 6".
std::string item(std::string_view name, std::size_t index, std::size_t total);

// Wording for DKR-R's own settings in the redesigned Options menu, written in
// capitals like the game's menu text. Game settings use the game's text.
enum class Phrase {
    GeneralOptions,
    MasterVolume,
    VehiclesVolume,
    NatureVolume,
    OutputDevice,
    SystemDefault,
    NotConnected,
    MultiplayerMusic,
    On,
    Off,
    Equaliser,
    Custom,
    Bass,
    Mid,
    Treble,
    Ready,
};
std::string phrase(Phrase phrase);

// A character by the game's Character id (include/enums.h); the character
// select screen shows them only as models. Empty for an unknown id.
std::string character_name(std::uint32_t character);

// A player by controller number, counted from 1: "PLAYER 2".
std::string player(std::size_t number);
// "PLAYER 2 LEFT".
std::string player_left(std::size_t number);

// A record time: "1 MINUTE 2.35 SECONDS", "2 MINUTES 0.05 SECONDS", or
// "20.11 SECONDS" under a minute.
std::string record_time(int minutes, int seconds, int hundredths);

// A save's balloon count: "1 BALLOON", "12 BALLOONS".
std::string balloons(int count);

// A volume level: "80%".
std::string percent(int value);
// An equaliser band level: "+3 dB", "0 dB", "-2 dB".
std::string decibels(int value);

} // namespace dkr::accessibility::text
