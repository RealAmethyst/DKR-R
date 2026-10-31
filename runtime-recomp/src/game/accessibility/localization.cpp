#include "localization.hpp"

namespace dkr::accessibility::text {

std::string position(std::size_t index, std::size_t total) {
    return std::to_string(index) + " of " + std::to_string(total);
}

std::string item(std::string_view name, std::size_t index, std::size_t total) {
    return std::string(name) + ", " + position(index, total);
}

std::string phrase(Phrase phrase) {
    switch (phrase) {
        case Phrase::GeneralOptions: return "GENERAL OPTIONS";
        case Phrase::MasterVolume: return "MASTER VOLUME";
        case Phrase::VehiclesVolume: return "VEHICLES VOLUME";
        case Phrase::NatureVolume: return "NATURE VOLUME";
        case Phrase::OutputDevice: return "OUTPUT DEVICE";
        case Phrase::SystemDefault: return "SYSTEM DEFAULT";
        case Phrase::NotConnected: return "NOT CONNECTED";
        case Phrase::MultiplayerMusic: return "MULTIPLAYER MUSIC";
        case Phrase::On: return "ON";
        case Phrase::Off: return "OFF";
        case Phrase::Equaliser: return "EQUALISER";
        case Phrase::Custom: return "CUSTOM";
        case Phrase::Bass: return "BASS";
        case Phrase::Mid: return "MID";
        case Phrase::Treble: return "TREBLE";
    }
    return {};
}

std::string percent(int value) {
    return std::to_string(value) + "%";
}

std::string decibels(int value) {
    return (value > 0 ? "+" : "") + std::to_string(value) + " dB";
}

} // namespace dkr::accessibility::text
