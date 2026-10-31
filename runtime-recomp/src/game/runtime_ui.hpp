#pragma once

#include "runtime_enhancements.hpp"

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dkr::mods { struct PreparedModLaunch; }
namespace dkr::mods::online { class RuntimeResources; }
namespace dkr::runtime::rom { struct Identity; }

struct SDL_Window;
typedef union SDL_Event SDL_Event;

namespace RT64 {
struct Application;
}

namespace dkr::runtime::ui {

enum class LifecycleRequest : std::uint8_t {
    None = 0,
    StopGame,
    Exit,
    Restart,
};

struct StartupResult {
    bool start_game = false;
    LifecycleRequest lifecycle_request = LifecycleRequest::None;
    std::filesystem::path rom_path;
    std::shared_ptr<const dkr::mods::PreparedModLaunch> mods;
};

void configure(const std::filesystem::path& config_directory);
void shutdown_online_mods();
// Game stopped / launch admission only. Empty means a vanilla manifest.
// Throws rather than silently launching if an accepted mod proof is missing.
std::shared_ptr<const dkr::mods::online::RuntimeResources> online_mod_resources();
void persist_settings();
// Settings shared with the redesigned in-game Options menu. Channels follow
// the Sound page: 0 master, 1 music, 2 sound effects, 3 vehicles, 4 nature.
float sound_level(std::size_t channel);
void set_sound_level(std::size_t channel, float level);
std::vector<std::string> sound_preset_labels();
// The equaliser preset matching the current curve; nullopt for a custom one.
std::optional<std::size_t> sound_preset();
bool sound_custom_curve_kept();
// A preset, or nullopt for the kept custom curve.
void apply_sound_preset(std::optional<std::size_t> preset);
// Bands 0 bass, 1 mid, 2 treble, in whole decibels (-12 to 12).
int sound_band(std::size_t band);
void set_sound_band(std::size_t band, int decibels);
void set_presentation_profile(enhancements::PresentationProfile profile);
// Called only after an explicitly selected renderer backend fails and RT64
// successfully recovers with Automatic. Persist the recovered choice so the
// next launch does not repeat the same failure loop.
void persist_graphics_api_fallback();
// The bounded software return path is used after experimental failure, and
// also after graceful experimental GPU retirement on Windows. It does not
// change the selected graphics API or settings for the next game launch.
StartupResult run_startup_screen(
    SDL_Window* window,
    const std::filesystem::path& preselected_rom = {},
    bool software_failure_recovery = false);

void attach(RT64::Application& application);
void detach(RT64::Application& application);
void draw(RT64::Application& application);
bool handle_runtime_event(SDL_Event* event);
// Once per event pump, after the events: captures the pointer for the Modern
// camera's mouse orbit while racing, and lets it go when anything needs it.
void update_camera_mouse();
bool input_capture_active();
void toggle_overlay();
bool overlay_visible();
LifecycleRequest lifecycle_request();
void reset_lifecycle_request();
void report_mod_error(std::string error);
// Main/event thread only, after both experimental game workers have joined.
void report_online_game_end(std::string message, bool lobby_retained);
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
// Isolated diagnostic entry: same manifest/save/host actions as the real UI.
bool configure_owned_online_check(const rom::Identity& identity,bool host,unsigned players,std::string& error);
#endif

} // namespace dkr::runtime::ui
