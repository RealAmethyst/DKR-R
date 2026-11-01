#pragma once

#include <ostream>
#include <string>

// The player's settings for the race sound cues (ACCESSIBILITY OPTIONS >
// AUDIO CUES), remembered in DKR-R's settings file. Read from any thread.
namespace dkr::race_cues {

enum class TurnDirection {
    ToTheSide,   // hard left or right
    FromTheTurn, // from the turn's position
};

struct CueSettings {
    bool edge_enabled = true;
    int edge_volume = 100; // percent, 0-100 in steps of 5
    int edge_distance = 5; // how far away the edge is heard, 1-10
    bool turn_enabled = true;
    int turn_volume = 100;
    TurnDirection turn_direction = TurnDirection::ToTheSide;
    int turn_warning_tenths = 20; // seconds x 10, 10-40 in steps of 5
    // The hub beacon's sound.
    bool beacon_enabled = true;
    int beacon_volume = 100;
};

constexpr int kVolumeStep = 5;
constexpr int kDistanceMin = 1;
constexpr int kDistanceMax = 10;
constexpr int kWarningMinTenths = 10;
constexpr int kWarningMaxTenths = 40;
constexpr int kWarningStepTenths = 5;

CueSettings settings();
// Where the edge sound starts, in world units, for TRACK EDGE DISTANCE: 5 is
// 160 units, about the width of a typical road.
float edge_range(int distance);

// Stores the settings, clamped to their ranges.
void set_settings(const CueSettings& settings);

// Settings file entries; false when the key is not one of these.
bool load_setting(const std::string& key, const std::string& value);
void write_settings(std::ostream& output);

} // namespace dkr::race_cues
