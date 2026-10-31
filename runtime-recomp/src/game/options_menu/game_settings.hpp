#pragma once

#include "game_calls.hpp"
#include "guest_calls.hpp"
#include "options_menu.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dkr::options_menu {

// The menu's items backed by the game's own settings (through its functions
// and variables) and DKR-R's settings (through the runtime). Labels use the
// game's text where the game has it, mod wording otherwise. Valid for one
// hook call: it holds the hook's guest call frame.
class GameSettings final : public Settings {
public:
    GameSettings(std::uint8_t* rdram, GuestCalls& calls, const GameCalls& game,
                 const std::vector<std::string>& output_devices);

    bool visible(ItemId id) const override;
    std::string heading(PageId page) const override;
    std::string label(ItemId id) const override;
    std::string value(ItemId id) const override;
    bool adjust(ItemId id, int step) override;
    std::optional<std::string> activate(ItemId id) override;

private:
    std::uint32_t game_volume(runtime::RecompiledEntrypoint getter) const;
    bool subtitles_on() const;
    std::optional<std::uint32_t> sound_mode() const;

    std::uint8_t* rdram_;
    GuestCalls& calls_;
    const GameCalls& game_;
    const std::vector<std::string>& output_devices_;
};

} // namespace dkr::options_menu
