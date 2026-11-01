#include "race_cues/cue_settings.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <optional>

namespace dkr::race_cues {
namespace {

std::uint64_t Pack(const CueSettings& settings) {
    return (settings.edge_enabled ? 1U : 0U) | (settings.turn_enabled ? 2U : 0U) |
           (settings.turn_direction == TurnDirection::FromTheTurn ? 4U : 0U) |
           (static_cast<std::uint64_t>(settings.edge_distance) << 3) |
           (static_cast<std::uint64_t>(settings.edge_volume) << 8) |
           (static_cast<std::uint64_t>(settings.turn_volume) << 16) |
           (static_cast<std::uint64_t>(settings.turn_warning_tenths) << 24) |
           (static_cast<std::uint64_t>(settings.beacon_enabled ? 1U : 0U) << 32) |
           (static_cast<std::uint64_t>(settings.beacon_volume) << 40);
}

CueSettings Unpack(std::uint64_t packed) {
    CueSettings settings;
    settings.edge_enabled = (packed & 1U) != 0;
    settings.turn_enabled = (packed & 2U) != 0;
    settings.turn_direction = (packed & 4U) != 0 ? TurnDirection::FromTheTurn : TurnDirection::ToTheSide;
    settings.edge_distance = static_cast<int>((packed >> 3) & 0x1FU);
    settings.edge_volume = static_cast<int>((packed >> 8) & 0xFFU);
    settings.turn_volume = static_cast<int>((packed >> 16) & 0xFFU);
    settings.turn_warning_tenths = static_cast<int>((packed >> 24) & 0xFFU);
    settings.beacon_enabled = ((packed >> 32) & 1U) != 0;
    settings.beacon_volume = static_cast<int>((packed >> 40) & 0xFFU);
    return settings;
}

// Packed so the audio thread reads them without a lock; writers serialise.
std::mutex g_mutex;
std::atomic<std::uint64_t> g_packed{Pack(CueSettings{})};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

CueSettings Clamped(CueSettings settings) {
    settings.edge_volume = std::clamp(settings.edge_volume, 0, 100);
    settings.edge_distance = std::clamp(settings.edge_distance, kDistanceMin, kDistanceMax);
    settings.turn_volume = std::clamp(settings.turn_volume, 0, 100);
    settings.beacon_volume = std::clamp(settings.beacon_volume, 0, 100);
    settings.turn_warning_tenths =
        std::clamp(settings.turn_warning_tenths, kWarningMinTenths, kWarningMaxTenths);
    return settings;
}

std::optional<int> Parse(const std::string& value, int minimum, int maximum) {
    int number = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || number < minimum ||
        number > maximum) {
        return std::nullopt;
    }
    return number;
}

} // namespace

float edge_range(int distance) {
    return 32.0F * static_cast<float>(std::clamp(distance, kDistanceMin, kDistanceMax));
}

CueSettings settings() {
    return Unpack(g_packed.load(std::memory_order_acquire));
}

void set_settings(const CueSettings& settings) {
    std::scoped_lock lock(g_mutex);
    g_packed.store(Pack(Clamped(settings)), std::memory_order_release);
}

bool load_setting(const std::string& key, const std::string& value) {
    std::scoped_lock lock(g_mutex);
    CueSettings loaded = settings();
    if (key == "race_cue_edge_enabled") {
        if (const auto number = Parse(value, 0, 1)) loaded.edge_enabled = *number != 0;
    } else if (key == "race_cue_edge_volume") {
        if (const auto number = Parse(value, 0, 100)) loaded.edge_volume = *number;
    } else if (key == "race_cue_edge_distance") {
        if (const auto number = Parse(value, kDistanceMin, kDistanceMax)) loaded.edge_distance = *number;
    } else if (key == "race_cue_turn_enabled") {
        if (const auto number = Parse(value, 0, 1)) loaded.turn_enabled = *number != 0;
    } else if (key == "race_cue_turn_volume") {
        if (const auto number = Parse(value, 0, 100)) loaded.turn_volume = *number;
    } else if (key == "race_cue_turn_direction") {
        if (const auto number = Parse(value, 0, 1)) {
            loaded.turn_direction = *number == 0 ? TurnDirection::ToTheSide : TurnDirection::FromTheTurn;
        }
    } else if (key == "race_cue_beacon_enabled") {
        if (const auto number = Parse(value, 0, 1)) loaded.beacon_enabled = *number != 0;
    } else if (key == "race_cue_beacon_volume") {
        if (const auto number = Parse(value, 0, 100)) loaded.beacon_volume = *number;
    } else if (key == "race_cue_turn_warning") {
        if (const auto number = Parse(value, kWarningMinTenths, kWarningMaxTenths)) {
            loaded.turn_warning_tenths = *number;
        }
    } else {
        return false;
    }
    g_packed.store(Pack(loaded), std::memory_order_release);
    return true;
}

void write_settings(std::ostream& output) {
    const CueSettings current = settings();
    output << "race_cue_edge_enabled=" << (current.edge_enabled ? 1 : 0) << '\n';
    output << "race_cue_edge_volume=" << current.edge_volume << '\n';
    output << "race_cue_edge_distance=" << current.edge_distance << '\n';
    output << "race_cue_turn_enabled=" << (current.turn_enabled ? 1 : 0) << '\n';
    output << "race_cue_turn_volume=" << current.turn_volume << '\n';
    output << "race_cue_turn_direction=" << (current.turn_direction == TurnDirection::ToTheSide ? 0 : 1)
           << '\n';
    output << "race_cue_turn_warning=" << current.turn_warning_tenths << '\n';
    output << "race_cue_beacon_enabled=" << (current.beacon_enabled ? 1 : 0) << '\n';
    output << "race_cue_beacon_volume=" << current.beacon_volume << '\n';
}

} // namespace dkr::race_cues
