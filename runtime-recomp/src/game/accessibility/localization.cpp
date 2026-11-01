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
        case Phrase::Ready: return "READY";
        case Phrase::AccessibilityOptions: return "ACCESSIBILITY OPTIONS";
        case Phrase::AudioCues: return "AUDIO CUES";
        case Phrase::TrackEdgeSound: return "TRACK EDGE SOUND";
        case Phrase::TrackEdgeVolume: return "TRACK EDGE VOLUME";
        case Phrase::TrackEdgeDistance: return "TRACK EDGE DISTANCE";
        case Phrase::TurnSignal: return "TURN SIGNAL";
        case Phrase::TurnSignalVolume: return "TURN SIGNAL VOLUME";
        case Phrase::TurnSignalDirection: return "TURN SIGNAL DIRECTION";
        case Phrase::ToTheSide: return "TO THE SIDE";
        case Phrase::FromTheTurn: return "FROM THE TURN";
        case Phrase::TurnWarningTime: return "TURN WARNING TIME";
        case Phrase::BeaconSound: return "BEACON SOUND";
        case Phrase::BeaconVolume: return "BEACON VOLUME";
        case Phrase::Taj: return "TAJ";
        case Phrase::Balloon: return "BALLOON";
        case Phrase::TimbersIsland: return "TIMBER'S ISLAND";
        case Phrase::GuidanceOff: return "GUIDANCE OFF";
        case Phrase::Ahead: return "AHEAD";
        case Phrase::AheadRight: return "AHEAD RIGHT";
        case Phrase::Right: return "RIGHT";
        case Phrase::BehindRight: return "BEHIND RIGHT";
        case Phrase::Behind: return "BEHIND";
        case Phrase::BehindLeft: return "BEHIND LEFT";
        case Phrase::Left: return "LEFT";
        case Phrase::AheadLeft: return "AHEAD LEFT";
    }
    return {};
}

std::string character_name(std::uint32_t character) {
    switch (character) {
        case 0: return "KRUNCH";
        case 1: return "BUMPER";
        case 2: return "TIPTUP";
        case 3: return "CONKER";
        case 4: return "TIMBER";
        case 5: return "BANJO";
        case 6: return "DRUMSTICK";
        case 7: return "PIPSY";
        case 8: return "T.T.";
        case 9: return "DIDDY";
    }
    return {};
}

std::string player(std::size_t number) {
    return "PLAYER " + std::to_string(number);
}

std::string player_left(std::size_t number) {
    return player(number) + " LEFT";
}

std::string record_time(int minutes, int seconds, int hundredths) {
    std::string result;
    if (minutes > 0) {
        result = std::to_string(minutes) + (minutes == 1 ? " MINUTE " : " MINUTES ");
    }
    const std::string fraction = std::to_string(hundredths);
    return result + std::to_string(seconds) + "." + (hundredths < 10 ? "0" : "") + fraction + " SECONDS";
}

std::string balloons(int count) {
    return std::to_string(count) + (count == 1 ? " BALLOON" : " BALLOONS");
}

std::string percent(int value) {
    return std::to_string(value) + "%";
}

std::string decibels(int value) {
    return (value > 0 ? "+" : "") + std::to_string(value) + " dB";
}

std::string seconds_tenths(int tenths) {
    const std::string fraction = tenths % 10 == 0 ? std::string{} : "." + std::to_string(tenths % 10);
    return std::to_string(tenths / 10) + fraction + (tenths == 10 ? " SECOND" : " SECONDS");
}

std::string guiding(std::string_view destination) {
    return "GUIDING, " + std::string(destination);
}

std::string arrived(std::string_view destination) {
    return "ARRIVED, " + std::string(destination);
}

std::string no_route(std::string_view destination) {
    return "NO ROUTE, " + std::string(destination);
}

std::string direction(std::string_view destination, Phrase bearing) {
    return std::string(destination) + ", " + phrase(bearing);
}

} // namespace dkr::accessibility::text
