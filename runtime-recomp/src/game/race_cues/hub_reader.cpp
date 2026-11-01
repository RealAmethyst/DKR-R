// Reads a hub world for the hub beacon once per authored frame. Read-only:
// no guest writes and no guest calls. Layouts are from the matching decomp
// (structs.h, level_object_entries.h, object_functions.c, joypad.c) and
// identical in both revisions; every global is looked up by name.
#include "race_cues/hub_reader.hpp"

#include "accessibility/game_text.hpp"
#include "accessibility/guest_text.hpp"
#include "accessibility/speech.hpp"
#include "accessibility/symbol_lookup.hpp"
#include "race_cues/cue_audio.hpp"
#include "race_cues/hub_beacon.hpp"
#include "race_cues/hub_route.hpp"
#include "revision_addresses.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>

namespace dkr::race_cues {
namespace {

using accessibility::guest_byte;
using accessibility::guest_half;
using accessibility::guest_word;
namespace symbols = accessibility::symbols;
namespace revision_addresses = runtime::revision_addresses;
namespace rom = runtime::rom;

// ObjectBehaviours (include/object_behaviors.h).
constexpr std::int16_t kBehaviourExit = 7;
constexpr std::int16_t kBehaviourDoor = 14;
constexpr std::int16_t kBehaviourTT = 31;       // BHV_STOPWATCH_MAN
constexpr std::int16_t kBehaviourTaj = 62;      // BHV_PARK_WARDEN
constexpr std::int16_t kBehaviourTrophy = 74;   // BHV_TROPHY_CABINET
constexpr std::int16_t kBehaviourBalloon = 77;  // BHV_GOLDEN_BALLOON
// ObjectTransform.flags: OBJ_FLAGS_PARTICLE objects are skipped, as
// find_taj_object does.
constexpr std::uint16_t kParticle = 0x8000;
// gObjPtrList's size (OBJECT_SLOT_COUNT, objects.c).
constexpr std::uint32_t kObjectSlots = 512;
// LevelGlobalData (game.h): 6 bytes, world then raceType.
constexpr std::uint32_t kLevelGlobalSize = 6;
constexpr std::int8_t kRaceDefault = 0;
constexpr std::int8_t kRaceHubWorld = 5;
constexpr std::int8_t kRaceBoss = 8;
constexpr std::int8_t kWorldCentralArea = 0;
// A gate or door belongs to the exit nearest to it, within this distance;
// they stand 75 to 500 units from their exits. Exits this close together
// (the boss door's first fight and rematch) share their doors.
constexpr float kGateReach = 1000.0F;
constexpr float kTogether = 100.0F;
// Object_Exit.bossFlag: WARP_BOSS_FIRST, WARP_BOSS_REMATCH. The first fight
// closes and the rematch opens once the world's 8 balloons are won
// (obj_loop_exit).
constexpr std::int8_t kBossFirst = 0;
constexpr std::int8_t kBossRematch = 1;
constexpr std::int16_t kWorldBalloons = 8;
// The trophy race's name (AssetMenuTextIdsEnum ASSET_MENU_TEXT_TROPHYRACE,
// the same in both revisions).
constexpr std::uint32_t kTrophyRaceText = 70;
// The boss door's name: the first line of game text 44, the text box every
// world hub's boss doors (ASSET_OBJECT_BOSSDOOR, LevelObjectEntry_Door.textID)
// show while closed (obj_loop_door: set_current_text(door->textID)).
constexpr std::uint32_t kBossDoorText = 44;
// Where the kart and a destination meet the route grid.
constexpr float kStartReach = 120.0F;
constexpr float kEndReach = 400.0F;
constexpr std::uint32_t kPlayers = 4;

struct Globals {
    rom::Revision revision{};
    std::uint32_t objects = 0;
    std::uint32_t object_count = 0;
    std::uint32_t object_start = 0;
    std::uint32_t settings = 0;
    std::uint32_t level_table = 0;
    std::uint32_t level_count = 0;
    std::uint32_t pressed = 0;
    std::uint32_t pads = 0;
    std::uint32_t player_ids = 0;
    std::uint32_t paused = 0;
};

std::uint32_t g_reported = 0;

void Report(std::uint32_t bit, const char* reason) {
    if ((g_reported & bit) != 0) return;
    g_reported |= bit;
    std::fprintf(stderr, "[boot][race-cues][hub] %s\n", reason);
}

std::optional<Globals> ResolveGlobals() {
    static std::optional<Globals> cached;
    const rom::Revision revision = revision_addresses::selected_revision();
    if (cached && cached->revision == revision) return cached;
    struct Want {
        std::uint32_t Globals::*field;
        const char* name;
        std::uint32_t size;
    };
    constexpr Want kWants[] = {
        {&Globals::objects, "gObjPtrList", 4},
        {&Globals::object_count, "gObjectCount", 4},
        {&Globals::object_start, "gObjectListStart", 4},
        {&Globals::settings, "gSettingsPtr", 4},
        {&Globals::level_table, "gGlobalLevelTable", 4},
        {&Globals::level_count, "gNumberOfLevelHeaders", 4},
        {&Globals::pressed, "gControllerButtonsPressed", 2 * kPlayers},
        {&Globals::pads, "gControllerCurrData", 6 * kPlayers},
        {&Globals::player_ids, "sPlayerID", 1},
        {&Globals::paused, "gIsPaused", 1},
    };
    Globals globals;
    globals.revision = revision;
    for (const Want& want : kWants) {
        const std::optional<std::uint32_t> address = symbols::data(want.name, want.size);
        if (!address) {
            std::fprintf(stderr, "[boot][race-cues][hub] off: symbol %s not found\n", want.name);
            return std::nullopt;
        }
        globals.*want.field = *address;
    }
    cached = globals;
    return cached;
}

std::optional<Vec3> GuestVec3(std::uint8_t* rdram, std::uint32_t address) {
    Vec3 value;
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const std::optional<std::uint32_t> word = guest_word(rdram, address + axis * 4);
        if (!word) return std::nullopt;
        const float component = std::bit_cast<float>(*word);
        if (!std::isfinite(component)) return std::nullopt;
        (axis == 0 ? value.x : axis == 1 ? value.y : value.z) = component;
    }
    return value;
}

// The player's progress (Settings, structs.h): the hub's course flags
// (courseFlagsPtr +0x04 [courseId +0x49]: door and balloon bits, set by
// obj_loop_door and obj_loop_goldenballoon), the world (+0x48), its balloons
// (balloonsPtr +0x00 [worldId]) and the bosses beaten (+0x0C).
struct Progress {
    std::uint32_t course_flags = 0;
    std::int32_t world = 0;
    std::int16_t world_balloons = 0;
    std::uint16_t bosses = 0;
};

std::optional<Progress> ReadProgress(std::uint8_t* rdram, const Globals& globals) {
    const std::optional<std::uint32_t> settings = guest_word(rdram, globals.settings);
    if (!settings || *settings == 0) return std::nullopt;
    const std::optional<std::uint32_t> balloons = guest_word(rdram, *settings + 0x00);
    const std::optional<std::uint32_t> flags = guest_word(rdram, *settings + 0x04);
    const std::optional<std::int16_t> bosses = guest_half(rdram, *settings + 0x0C);
    const std::optional<std::int8_t> world = guest_byte(rdram, *settings + 0x48);
    const std::optional<std::int8_t> course = guest_byte(rdram, *settings + 0x49);
    if (!balloons || !flags || !bosses || !world || !course || *balloons == 0 || *flags == 0) return std::nullopt;
    const auto world_id = static_cast<std::uint32_t>(static_cast<std::uint8_t>(*world));
    const std::optional<std::uint32_t> course_flags =
        guest_word(rdram, *flags + static_cast<std::uint32_t>(static_cast<std::uint8_t>(*course)) * 4);
    const std::optional<std::int16_t> world_balloons = guest_half(rdram, *balloons + world_id * 2);
    if (!course_flags || !world_balloons) return std::nullopt;
    return Progress{*course_flags, static_cast<std::int32_t>(world_id), *world_balloons,
                    static_cast<std::uint16_t>(*bosses)};
}

struct Exit {
    std::int32_t level = 0;
    std::int8_t boss = -1;
    Vec3 position;
    bool open = true;
    bool guarded = false;
    // The fewest balloons a door in front of it first asked for.
    std::int32_t order = 0x7FFF;
};

struct Door {
    Vec3 position;
    bool open = false;
    std::int32_t balloons = 0;
};

struct HubObjects {
    std::vector<Exit> exits;
    std::vector<Door> doors;
    std::optional<Destination> character;
    std::optional<Destination> trophy;
    std::vector<HubBalloon> balloons;
};

std::optional<HubObjects> ReadObjects(std::uint8_t* rdram, const Globals& globals, const Progress& progress,
                                      std::int32_t hub_world) {
    const std::optional<std::uint32_t> list = guest_word(rdram, globals.objects);
    const std::optional<std::uint32_t> count = guest_word(rdram, globals.object_count);
    const std::optional<std::uint32_t> start = guest_word(rdram, globals.object_start);
    if (!list || !count || !start || *list == 0 || *count > kObjectSlots || *start > *count) {
        Report(1U << 1, "object list unreadable or out of range");
        return std::nullopt;
    }
    const std::uint32_t flags = progress.course_flags;
    HubObjects objects;
    for (std::uint32_t index = *start; index < *count; ++index) {
        const std::optional<std::uint32_t> object = guest_word(rdram, *list + index * 4);
        if (!object || *object == 0) continue;
        // Object: trans.flags +0x06, position +0x0C, level_entry +0x3C,
        // behaviorId +0x48, the behaviour's data +0x64, properties +0x78.
        const std::optional<std::int16_t> object_flags = guest_half(rdram, *object + 0x06);
        if (!object_flags || (static_cast<std::uint16_t>(*object_flags) & kParticle) != 0) continue;
        const std::optional<std::int16_t> behaviour = guest_half(rdram, *object + 0x48);
        if (!behaviour) continue;
        if (*behaviour != kBehaviourExit && *behaviour != kBehaviourDoor && *behaviour != kBehaviourTT &&
            *behaviour != kBehaviourTaj && *behaviour != kBehaviourTrophy && *behaviour != kBehaviourBalloon) {
            continue;
        }
        const std::optional<Vec3> position = GuestVec3(rdram, *object + 0x0C);
        if (!position) continue;
        const std::optional<std::uint32_t> entry = guest_word(rdram, *object + 0x3C);
        if (*behaviour == kBehaviourExit) {
            // LevelObjectEntry_Exit.destinationMapId +0x08; Object_Exit
            // (+0x64) bossFlag +0x14.
            const std::optional<std::int8_t> level = entry ? guest_byte(rdram, *entry + 0x08) : std::nullopt;
            const std::optional<std::uint32_t> exit = guest_word(rdram, *object + 0x64);
            const std::optional<std::int8_t> boss = exit ? guest_byte(rdram, *exit + 0x14) : std::nullopt;
            if (!level || !boss) continue;
            Exit found;
            found.level = static_cast<std::uint8_t>(*level);
            found.boss = *boss;
            found.position = *position;
            objects.exits.push_back(found);
        } else if (*behaviour == kBehaviourDoor) {
            // Object_Door (+0x64) doorID +0x0E: unlocked while course flag
            // 0x10000 << doorID is set, which obj_loop_door keeps up to date
            // from the balloons, keys and boss wins it asks for. A door
            // without an id never opens. LevelObjectEntry_Door.balloonCount
            // +0x0D is what it first asks for.
            const std::optional<std::uint32_t> door = guest_word(rdram, *object + 0x64);
            const std::optional<std::int8_t> id = door ? guest_byte(rdram, *door + 0x0E) : std::nullopt;
            const std::optional<std::int8_t> balloons = entry ? guest_byte(rdram, *entry + 0x0D) : std::nullopt;
            if (!id || !balloons) continue;
            const bool open = *id >= 0 && *id < 16 && (flags & (0x10000U << *id)) != 0;
            objects.doors.push_back({*position, open, static_cast<std::uint8_t>(*balloons)});
        } else if (*behaviour == kBehaviourTaj || *behaviour == kBehaviourTT) {
            // Taj in Timber's Island, T.T. in the world hubs. Each has met
            // the player once properties.*.action (+0x78) leaves ROAM (0):
            // TAJ_MODE_APPROACH_PLAYER to TAJ_MODE_TELEPORT_AWAY_END (1 to 11,
            // obj_loop_parkwarden), TT_MODE_APPROACH_PLAYER to
            // TT_MODE_DIALOGUE_END (1 to 4, obj_loop_stopwatchman).
            const bool taj = *behaviour == kBehaviourTaj;
            if (objects.character || taj != (hub_world == kWorldCentralArea)) continue;
            const std::optional<std::uint32_t> action = guest_word(rdram, *object + 0x78);
            Destination character;
            character.kind = taj ? DestinationKind::Taj : DestinationKind::TT;
            character.position = *position;
            character.reached = action && *action >= 1 && *action <= (taj ? 11U : 4U);
            objects.character = character;
        } else if (*behaviour == kBehaviourTrophy) {
            // Open once the world's 8 balloons and its boss rematch are won
            // (obj_loop_trophycab: bosses bit worldId + 6, 0x800 always set);
            // its menu is open while properties.trophyCabinet.action (+0x78)
            // is 1, and 2 once the race starts.
            const std::uint32_t bosses = progress.bosses | 0x800U;
            const bool open = progress.world_balloons >= kWorldBalloons && progress.world >= 0 &&
                              progress.world < 10 && (bosses & (1U << (progress.world + 6))) != 0;
            if (!open || objects.trophy || hub_world == kWorldCentralArea) continue;
            const std::optional<std::uint32_t> action = guest_word(rdram, *object + 0x78);
            const std::optional<std::string> name = accessibility::menu_text(rdram, kTrophyRaceText);
            if (!name || name->empty()) {
                Report(1U << 6, "the trophy race's name could not be read; it is left out");
                continue;
            }
            objects.trophy = Destination{DestinationKind::TrophyRace, 0, *name, *position,
                                         action && (*action == 1 || *action == 2)};
        } else {
            // LevelObjectEntry_GoldenBalloon.balloonID +0x08; collected when
            // course flag 0x10000 << balloonID is set. A Taj prize whose
            // challenge is not won has properties.goldenBalloon.action 1:
            // hidden and out of reach (obj_init_goldenballoon).
            const std::optional<std::int8_t> id = entry ? guest_byte(rdram, *entry + 0x08) : std::nullopt;
            const std::optional<std::uint32_t> action = guest_word(rdram, *object + 0x78);
            if (!id || !action || *id < 0 || *id >= 16) continue;
            if (*action != 0 || (flags & (0x10000U << *id)) != 0) continue;
            objects.balloons.push_back({*id, *position});
        }
    }
    return objects;
}

// The entrances: exits with a gate or door in front. In Timber's Island the
// ways into a world's hub (gGlobalLevelTable: raceType RACETYPE_HUBWORLD,
// world past the central area); in a world hub its races (RACETYPE_DEFAULT),
// the boss door's live entrance (RACETYPE_BOSS) and the way back to the
// island (the hub of WORLD_CENTRAL_AREA). Each is closed while a door in
// front of it is.
std::vector<Entrance> Entrances(std::uint8_t* rdram, const Globals& globals, HubObjects& objects,
                                const Progress& progress, std::int32_t hub_world) {
    for (const Door& door : objects.doors) {
        const Exit* nearest = nullptr;
        float best = kGateReach;
        for (const Exit& exit : objects.exits) {
            const float across = std::hypot(exit.position.x - door.position.x, exit.position.z - door.position.z);
            if (across < best) {
                best = across;
                nearest = &exit;
            }
        }
        if (nearest == nullptr) continue;
        const Vec3 at = nearest->position;
        for (Exit& exit : objects.exits) {
            if (std::hypot(exit.position.x - at.x, exit.position.z - at.z) > kTogether) continue;
            exit.guarded = true;
            exit.order = std::min(exit.order, door.balloons);
            if (!door.open) exit.open = false;
        }
    }
    std::vector<Entrance> entrances;
    const std::optional<std::uint32_t> table = guest_word(rdram, globals.level_table);
    const std::optional<std::uint32_t> levels = guest_word(rdram, globals.level_count);
    if (!table || !levels || *table == 0) {
        Report(1U << 2, "level table unreadable");
        return entrances;
    }
    const bool central = hub_world == kWorldCentralArea;
    for (const Exit& exit : objects.exits) {
        if (!exit.guarded || static_cast<std::uint32_t>(exit.level) >= *levels) continue;
        const std::uint32_t entry = *table + static_cast<std::uint32_t>(exit.level) * kLevelGlobalSize;
        const std::optional<std::int8_t> world = guest_byte(rdram, entry + 0);
        const std::optional<std::int8_t> race_type = guest_byte(rdram, entry + 1);
        if (!world || !race_type) continue;
        Entrance entrance;
        entrance.level = exit.level;
        entrance.position = exit.position;
        entrance.open = exit.open;
        if (central && *race_type == kRaceHubWorld && *world > kWorldCentralArea) {
            entrance.kind = DestinationKind::World;
            entrance.order = *world;
        } else if (!central && *race_type == kRaceDefault) {
            entrance.kind = DestinationKind::Track;
            entrance.order = exit.order;
        } else if (!central && *race_type == kRaceBoss) {
            // Only the boss exit the game would take now.
            const bool rematch = progress.world_balloons >= kWorldBalloons;
            if (exit.boss != (rematch ? kBossRematch : kBossFirst)) continue;
            entrance.kind = DestinationKind::Boss;
        } else if (!central && *race_type == kRaceHubWorld && *world == kWorldCentralArea) {
            entrance.kind = DestinationKind::Island;
        } else {
            continue;
        }
        if (entrance.kind == DestinationKind::World || entrance.kind == DestinationKind::Track) {
            std::optional<std::string> name = accessibility::level_name(rdram, exit.level);
            if (!name || name->empty()) Report(1U << 3, "a level's name could not be read; it is left out");
            entrance.name = name.value_or(std::string{});
        } else if (entrance.kind == DestinationKind::Boss) {
            std::optional<std::string> name = accessibility::game_textbox_line(rdram, kBossDoorText, 0);
            if (!name) Report(1U << 7, "the boss door's name could not be read; it is left out");
            entrance.name = name.value_or(std::string{});
        }
        entrances.push_back(entrance);
    }
    return entrances;
}

struct Graph {
    std::uint64_t key = 0;
    std::optional<RouteGraph> graph;
};

HubBeacon g_beacon;
Graph g_graph;
bool g_quiet = true;
// The hub the beacon is in; another hub starts over.
std::int32_t g_world = -1;

const RouteGraph* Routes(const LevelGeometry* geometry, std::uint64_t key) {
    if (geometry == nullptr) return nullptr;
    if (g_graph.graph && g_graph.key == key) return &*g_graph.graph;
    const auto begin = std::chrono::steady_clock::now();
    g_graph.graph.emplace(*geometry);
    g_graph.key = key;
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::fprintf(stderr, "[boot][race-cues][hub] route grid: %zu nodes, %.1f MiB, built in %.0f ms\n",
                 g_graph.graph->nodes(), static_cast<double>(g_graph.graph->memory_bytes()) / 1048576.0,
                 milliseconds);
    return &*g_graph.graph;
}

double Seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Speak(const std::vector<Utterance>& speech) {
    for (const Utterance& utterance : speech) accessibility::speech::say(utterance.text, utterance.interrupt);
}

void Publish(const BeaconOutput& output, const HubKart& kart) {
    BeaconFrame frame;
    frame.active = output.sounding;
    frame.pitch = output.pitch;
    if (output.sounding) {
        const float length = std::hypot(kart.forward.x, kart.forward.z);
        const float fx = length > 0.0F ? kart.forward.x / length : 0.0F;
        const float fz = length > 0.0F ? kart.forward.z / length : 1.0F;
        const float dx = output.aim.x - kart.position.x;
        const float dz = output.aim.z - kart.position.z;
        // The listener frame: +x right, +y up, -z ahead. Facing +Z, +X is
        // left: the right of (fx, fz) is (-fz, fx).
        float right = dx * -fz + dz * fx;
        if (kart.mirrored) right = -right;
        frame.direction = {right, output.aim.y - kart.position.y, -(dx * fx + dz * fz)};
        if (frame.direction.x == 0.0F && frame.direction.y == 0.0F && frame.direction.z == 0.0F) {
            frame.direction.z = -1.0F;
        }
    }
    publish_beacon(frame);
    g_quiet = !output.sounding;
}

} // namespace

void quiet_hub() {
    if (g_quiet) return;
    publish_beacon({});
    g_quiet = true;
}

void leave_hub() {
    quiet_hub();
    g_beacon.reset();
    g_world = -1;
}

void capture_hub(std::uint8_t* rdram, const HubKart& kart, const LevelGeometry* geometry,
                 std::uint64_t geometry_key) {
    const std::optional<Globals> globals = ResolveGlobals();
    if (!globals) {
        Report(1U << 0, "off: game symbols missing");
        quiet_hub();
        return;
    }
    // The pause menu owns the screen: no beacon, and the buttons wait.
    const std::optional<std::int8_t> paused = guest_byte(rdram, globals->paused);
    if (!paused || *paused != 0) {
        quiet_hub();
        return;
    }
    if (kart.world != g_world) {
        g_beacon.reset();
        g_world = kart.world;
    }
    const std::optional<Progress> progress = ReadProgress(rdram, *globals);
    if (!progress) {
        Report(1U << 4, "the player's progress is unreadable");
        quiet_hub();
        return;
    }
    std::optional<HubObjects> objects = ReadObjects(rdram, *globals, *progress, kart.world);
    if (!objects) {
        quiet_hub();
        return;
    }
    // Player 1's controller (input_pressed(PLAYER_ONE)): sPlayerID[0], then
    // gControllerButtonsPressed and gControllerCurrData (OSContPad, 6 bytes,
    // button +0) for it. These keep the game's own button mapping.
    BeaconInput input;
    const std::optional<std::int8_t> pad = guest_byte(rdram, globals->player_ids);
    if (pad && static_cast<std::uint8_t>(*pad) < kPlayers) {
        const auto id = static_cast<std::uint32_t>(static_cast<std::uint8_t>(*pad));
        const std::optional<std::int16_t> pressed = guest_half(rdram, globals->pressed + id * 2);
        const std::optional<std::int16_t> held = guest_half(rdram, globals->pads + id * 6);
        if (pressed && held) {
            input.pressed = static_cast<std::uint16_t>(*pressed);
            input.held = static_cast<std::uint16_t>(*held);
        }
    } else {
        Report(1U << 5, "player 1's controller unreadable");
    }
    input.kart = kart.position;
    input.forward = kart.forward;
    input.speed = kart.speed;
    input.mirrored = kart.mirrored;
    input.seconds = Seconds();
    input.course_flags = progress->course_flags;
    HubContents contents;
    contents.entrances = Entrances(rdram, *globals, *objects, *progress, kart.world);
    contents.character = objects->character;
    contents.trophy_race = objects->trophy;
    contents.balloons = objects->balloons;
    input.destinations = destination_list(std::move(contents), kart.position);

    const RoutePlanner plan = [&](const Vec3& from, const Vec3& to) -> std::optional<std::vector<Vec3>> {
        // Planes fly straight.
        if (kart.vehicle == Vehicle::Plane) return std::vector<Vec3>{from};
        const RouteGraph* routes = Routes(geometry, geometry_key);
        if (routes == nullptr) return std::nullopt;
        return routes->route(from, to, kStartReach, kEndReach);
    };
    const BeaconOutput output = g_beacon.update(input, plan);
    Speak(output.speech);
    Publish(output, kart);
}

} // namespace dkr::race_cues
