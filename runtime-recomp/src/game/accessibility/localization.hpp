#pragma once

#include <cstddef>
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
};
std::string phrase(Phrase phrase);

// A volume level: "80%".
std::string percent(int value);
// An equaliser band level: "+3 dB", "0 dB", "-2 dB".
std::string decibels(int value);

} // namespace dkr::accessibility::text
