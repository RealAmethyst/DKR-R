#pragma once

#include "race_cues/level_geometry.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Finds the bends of a course from its checkpoint gates and decides when the
// turn signal plays. Gates are CheckpointNode (objects.h): the game's course
// arrows (unk3B, the HUD's CourseIndicatorArrows in game_ui.h) mark bends;
// gate sets without any arrow use the course's own shape.
namespace dkr::race_cues {

struct Gate {
    Vec3 position;
    // CourseIndicatorArrows: 1-3 left, 4-6 right (45 degrees, sharp,
    // U-turn), 7 up, 8 down, 9 "!".
    std::uint8_t arrow = 0;

    bool operator==(const Gate&) const = default;
};

enum class TurnSide : std::int8_t { Left = -1, Right = 1 };

struct Bend {
    std::size_t gate = 0;
    TurnSide side = TurnSide::Left;
};

// The main route's gates in order (gTrackCheckpoints, the first
// gNumberOfMainCheckpoints). Sides are in world terms (not mirrored).
std::vector<Bend> find_bends(const std::vector<Gate>& gates);

struct Progress {
    // Object_Racer.nextCheckpoint and checkpoint_distance (1 just past the
    // previous gate, 0 at the next one).
    std::size_t next_gate = 0;
    float fraction = 0.0F;
    // Speed in units per second.
    float speed = 0.0F;
    // No signal while on an alternate route, facing the wrong way, or before
    // the race is under way; the bend ahead is still followed.
    bool quiet = false;
};

struct TurnSignal {
    TurnSide side = TurnSide::Left;
    Vec3 position; // the bend's gate
};

class TurnTracker {
public:
    // A new or changed course; forgets what has played.
    void set_course(std::vector<Gate> gates);
    const std::vector<Gate>& gates() const { return gates_; }
    const std::vector<Bend>& bends() const { return bends_; }
    void reset();

    // Once per update: the signal to play now, once per bend, when the bend is
    // `lead_seconds` or less away at the current speed.
    std::optional<TurnSignal> update(const Progress& progress, float lead_seconds);

private:
    std::vector<Gate> gates_;
    std::vector<Bend> bends_;
    std::optional<std::size_t> upcoming_;
    std::size_t gates_ahead_ = 0;
    bool signalled_ = false;
};

} // namespace dkr::race_cues
