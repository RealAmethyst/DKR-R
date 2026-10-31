#include "options_screen.hpp"

#include "accessibility/symbol_lookup.hpp"
#include "accessibility/guest_text.hpp"
#include "accessibility/speech.hpp"
#include "game_audio_memory.hpp"
#include "game_calls.hpp"
#include "game_settings.hpp"
#include "guest_calls.hpp"
#include "options_menu.hpp"
#include "revision_addresses.hpp"
#include "runtime_platform.hpp"
#include "runtime_ui.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace dkr::options_menu {

const GameCalls& game_calls() {
    return runtime::revision_addresses::selected_revision() == runtime::rom::Revision::UsV80 ? game_calls_v80()
                                                                                           : game_calls_v77();
}

namespace {

// Values from the decomp (src/menu.h, include/PR/os_cont.h, include/sounds.h,
// src/font.h); the stock Options screen uses the same ones.
constexpr std::int32_t kMenuTitle = 0;
constexpr std::int32_t kMenuMagicCodes = 10;
constexpr std::int32_t kMenuOptions = 12;
constexpr std::int32_t kMenuSaveOptions = 14;
constexpr std::int32_t kMenuBoot = 26;
constexpr std::uint32_t kButtonA = 0x8000U;
constexpr std::uint32_t kButtonB = 0x4000U;
constexpr std::uint32_t kButtonStart = 0x1000U;
constexpr std::uint32_t kSoundMove = 0xEBU;   // SOUND_MENU_PICK2
constexpr std::uint32_t kSoundSelect = 0xEFU; // SOUND_SELECT2
constexpr std::uint32_t kSoundBack = 0x241U;  // SOUND_MENU_BACK3
constexpr std::uint32_t kFunFont = 0U;
constexpr std::uint32_t kBigFont = 2U;
constexpr std::uint32_t kAlignMiddleCentre = 12U;
constexpr std::uint32_t kCentred = static_cast<std::uint32_t>(-0x8000); // POS_CENTRED
// optionscreen_render's layout: heading at the top, rows from y 76 every 28.
constexpr std::uint32_t kFirstRowY = 76U;
constexpr std::uint32_t kRowSpacing = 28U;
constexpr std::size_t kVisibleRows = 6U;
// About as many FUNFONT characters as fit across the screen.
constexpr std::size_t kRowCharacters = 28U;

struct Symbols {
    std::uint32_t display_list; // sMenuCurrDisplayList
    std::uint32_t blink;        // gOptionBlinkTimer
    std::uint32_t delay;        // gMenuDelay
    std::uint32_t ignore_input; // gIgnorePlayerInputTime
    std::uint32_t x_axis;       // gControllersXAxisDirection (s8[4])
    std::uint32_t y_axis;       // gControllersYAxisDirection (s8[4])
    std::uint32_t fade_in;      // sMenuTransitionFadeIn
};

std::optional<Symbols> resolve_symbols() {
    using accessibility::symbols::data;
    const auto display_list = data("sMenuCurrDisplayList", 4U);
    const auto blink = data("gOptionBlinkTimer", 4U);
    const auto delay = data("gMenuDelay", 4U);
    const auto ignore_input = data("gIgnorePlayerInputTime", 4U);
    const auto x_axis = data("gControllersXAxisDirection", 4U);
    const auto y_axis = data("gControllersYAxisDirection", 4U);
    const auto fade_in = data("sMenuTransitionFadeIn", 8U);
    if (!display_list || !blink || !delay || !ignore_input || !x_axis || !y_axis || !fade_in) {
        std::fprintf(stderr, "[options] a menu symbol is missing; the stock Options menu runs\n");
        return std::nullopt;
    }
    return Symbols{*display_list, *blink, *delay, *ignore_input, *x_axis, *y_axis, *fade_in};
}

struct ScreenState {
    Controller controller;
    std::vector<std::string> output_devices;
    std::optional<ItemId> leaving;
    std::size_t first_row = 0;
    bool active = false;
    bool audio_restored = false;
};

ScreenState& state() {
    static ScreenState value;
    return value;
}

gpr guest_register(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}

std::int32_t read_s32(std::uint8_t* rdram, std::uint32_t address) {
    return MEM_W(0, guest_register(address));
}

void write_s32(std::uint8_t* rdram, std::uint32_t address, std::int32_t value) {
    MEM_W(0, guest_register(address)) = value;
}

std::int32_t read_s8(std::uint8_t* rdram, std::uint32_t address) {
    return MEM_B(0, guest_register(address));
}

void speak(const std::vector<accessibility::Utterance>& speech) {
    for (const auto& utterance : speech) accessibility::speech::say(utterance.text, utterance.interrupt);
}

// The game forgets its own audio settings at power-on; give back the ones the
// player chose, and retire DKR-R's duplicate music and effects mix at 100%
// (the game's MUSIC VOLUME and SFX VOLUME are the sliders now).
void restore_game_audio(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game) {
    const GameAudio audio = remembered_game_audio();
    if (audio.music_volume) calls.call(game.music_volume_config_set, {*audio.music_volume});
    if (audio.sfx_volume) calls.call(game.sndp_set_global_volume, {*audio.sfx_volume});
    if (audio.sound_mode) {
        if (const auto symbol = accessibility::symbols::data("gAudioOutputType", 4U)) {
            write_s32(rdram, *symbol, static_cast<std::int32_t>(*audio.sound_mode));
            calls.call(game.set_stereo_pan_mode, {*audio.sound_mode});
        }
    }
    bool retired = false;
    for (const std::size_t channel : {std::size_t{1}, std::size_t{2}}) {
        if (runtime::ui::sound_level(channel) != 1.0F) {
            runtime::ui::set_sound_level(channel, 1.0F);
            retired = true;
        }
    }
    if (retired) runtime::ui::persist_settings();
}

std::string drawable(const std::string& text) {
    return text.size() <= kRowCharacters ? text : text.substr(0, kRowCharacters - 1U) + ".";
}

void draw(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game, const Symbols& symbols,
          const Settings& settings, ScreenState& screen) {
    const std::uint32_t list = symbols.display_list;
    auto text_at = [&](std::uint32_t x, std::uint32_t y, const std::string& value) {
        if (value.empty()) return;
        if (const auto address = calls.text(drawable(value))) {
            calls.call(game.draw_text, {list, x, y, *address, kAlignMiddleCentre});
        }
    };
    // The heading with its shadow, as optionscreen_render draws OPTIONS.
    const std::string heading = screen.controller.heading(settings);
    calls.call(game.set_text_font, {kBigFont});
    calls.call(game.set_text_background_colour, {0U, 0U, 0U, 0U});
    calls.call(game.set_text_colour, {0U, 0U, 0U, 255U, 128U});
    text_at(161U, 35U, heading);
    calls.call(game.set_text_colour, {255U, 255U, 255U, 0U, 255U});
    text_at(160U, 32U, heading);

    const std::vector<Row> rows = screen.controller.rows(settings);
    std::size_t selected = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].selected) selected = i;
    }
    // Scroll so the selected row stays on screen.
    if (selected < screen.first_row) screen.first_row = selected;
    if (selected >= screen.first_row + kVisibleRows) screen.first_row = selected + 1U - kVisibleRows;
    if (screen.first_row + kVisibleRows > rows.size()) {
        screen.first_row = rows.size() > kVisibleRows ? rows.size() - kVisibleRows : 0U;
    }
    const auto blink = static_cast<std::uint32_t>(read_s32(rdram, symbols.blink));
    calls.call(game.set_text_font, {kFunFont});
    for (std::size_t i = screen.first_row; i < rows.size() && i < screen.first_row + kVisibleRows; ++i) {
        std::uint32_t highlight = 0;
        if (rows[i].selected) {
            highlight = blink * 8U;
            if (blink >= 32U) highlight = 511U - highlight;
        }
        calls.call(game.set_text_colour, {255U, 255U, 255U, highlight, 255U});
        text_at(kCentred, kFirstRowY + static_cast<std::uint32_t>(i - screen.first_row) * kRowSpacing, rows[i].text);
    }
    calls.call(game.set_text_colour, {255U, 255U, 255U, 0U, 255U});
}

std::optional<Input> read_input(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game,
                                const Symbols& symbols) {
    // As menu_options_loop: every controller's presses and stick directions.
    std::uint32_t buttons = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    for (std::uint32_t player = 0; player < 4U; ++player) {
        buttons |= calls.call(game.input_pressed, {player});
        x += read_s8(rdram, symbols.x_axis + player);
        y += read_s8(rdram, symbols.y_axis + player);
    }
    if (buttons & kButtonB) return Input::Back;
    if (buttons & (kButtonA | kButtonStart)) return Input::Accept;
    if (x != 0) return x < 0 ? Input::Left : Input::Right;
    if (y != 0) return y < 0 ? Input::Down : Input::Up;
    return std::nullopt;
}

std::uint32_t sound_id(Sound sound) {
    switch (sound) {
        case Sound::Move: return kSoundMove;
        case Sound::Select: return kSoundSelect;
        case Sound::Back: return kSoundBack;
        case Sound::None: break;
    }
    return 0U;
}

} // namespace

void screen_opened(std::uint8_t* rdram, const recomp_context& context, std::int32_t menu_id) {
    ScreenState& screen = state();
    GuestCalls calls(rdram, context);
    if (!calls.valid()) return;
    const GameCalls& game = game_calls();
    // After the boot screen, once the game's audio is running (the logos
    // already play music).
    if (!screen.audio_restored && menu_id != kMenuBoot) {
        screen.audio_restored = true;
        restore_game_audio(rdram, calls, game);
    }
    screen.active = menu_id == kMenuOptions && resolve_symbols().has_value();
    if (!screen.active) return;
    screen.leaving.reset();
    screen.first_row = 0;
    screen.output_devices = runtime::platform::audio_output_devices();
    GameSettings settings(rdram, calls, game, screen.output_devices);
    speak(screen.controller.open(settings).speech);
}

} // namespace dkr::options_menu

extern "C" int dkr_options_menu_loop(std::uint8_t* rdram, recomp_context* context) {
    using namespace dkr::options_menu;
    ScreenState& screen = state();
    if (rdram == nullptr || context == nullptr || !screen.active) return 0;
    const std::optional<Symbols> symbols = resolve_symbols();
    GuestCalls calls(rdram, *context);
    if (!symbols || !calls.valid()) return 0;
    const GameCalls& game = game_calls();
    GameSettings settings(rdram, calls, game, screen.output_devices);

    // Timers exactly as menu_options_loop keeps them.
    const auto rate = static_cast<std::int32_t>(context->r4);
    write_s32(rdram, symbols->blink, (read_s32(rdram, symbols->blink) + rate) & 0x3F);
    std::int32_t delay = read_s32(rdram, symbols->delay);
    if (delay != 0) {
        delay += delay > 0 ? rate : -rate;
        write_s32(rdram, symbols->delay, delay);
    }
    if (delay > -20 && delay < 35) draw(rdram, calls, game, *symbols, settings, screen);

    if (read_s32(rdram, symbols->ignore_input) == 0 && delay == 0) {
        if (const std::optional<Input> input = read_input(rdram, calls, game, *symbols)) {
            if (*input == Input::Left || *input == Input::Right) {
                // A device may have been connected since the page opened.
                screen.output_devices = dkr::runtime::platform::audio_output_devices();
            }
            const Result result = screen.controller.handle(*input, settings);
            if (result.sound != Sound::None) calls.call(game.sound_play, {sound_id(result.sound), 0U});
            speak(result.speech);
            if (result.changed) dkr::runtime::ui::persist_settings();
            if (result.leave == ItemId::Return) {
                calls.call(game.music_fade, {static_cast<std::uint32_t>(-128)});
                delay = -1;
                write_s32(rdram, symbols->delay, delay);
                calls.call(game.transition_begin, {symbols->fade_in});
            } else if (result.leave) {
                screen.leaving = result.leave;
                delay = 31;
                write_s32(rdram, symbols->delay, delay);
            }
        }
    }

    if (delay > 30 && screen.leaving) {
        // Change to the game's own Save Options or Magic Codes screen.
        screen.active = false;
        calls.call(game.optionscreen_free, {});
        calls.call(game.menu_init, {static_cast<std::uint32_t>(
                                        *screen.leaving == ItemId::SaveOptions ? kMenuSaveOptions : kMenuMagicCodes)});
    } else if (delay < -30) {
        // Back to the title screen.
        screen.active = false;
        calls.call(game.music_change_on, {});
        calls.call(game.optionscreen_free, {});
        calls.call(game.menu_init, {static_cast<std::uint32_t>(kMenuTitle)});
    } else {
        write_s32(rdram, symbols->ignore_input, 0);
    }
    context->r2 = 0; // MENU_RESULT_CONTINUE
    return 1;
}
