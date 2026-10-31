#pragma once

#include "game_payload.hpp"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace dkr::options_menu {

// Calls into the recompiled game from a hook, following the pattern of
// mods/legacy_character_menu_render.cpp: a copy of the hook's context, a call
// frame reserved 0x400 bytes below the live stack, arguments by the O32 ABI
// (r4-r7, then the stack from sp+16). Strings are written into scratch space
// inside that frame, so they must be used before the hook returns.
class GuestCalls {
public:
    GuestCalls(std::uint8_t* rdram, const recomp_context& context);

    // False when the live stack cannot host a call frame; then nothing runs.
    bool valid() const { return valid_; }

    std::uint32_t call(runtime::RecompiledEntrypoint function, std::initializer_list<std::uint32_t> arguments);

    // The game font's drawable subset of text, written to scratch: printable
    // ASCII, anything else as '?'. Returns the guest address.
    std::optional<std::uint32_t> text(std::string_view value);

private:
    std::uint8_t* rdram_;
    recomp_context saved_;
    std::uint32_t frame_ = 0;
    bool valid_ = false;
};

} // namespace dkr::options_menu
