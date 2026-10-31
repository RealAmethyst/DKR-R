#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dkr::accessibility {

// Guest RDRAM as the recompiled game addresses it (KSEG0, 8 MiB).
constexpr std::uint32_t kGuestBegin = 0x80000000U;
constexpr std::uint32_t kGuestEnd = 0x80800000U;

constexpr bool guest_readable(std::uint32_t address, std::uint32_t bytes) {
    return address >= kGuestBegin && bytes <= kGuestEnd - kGuestBegin && address <= kGuestEnd - bytes;
}

// Read-only, range-checked guest reads; nullopt instead of a bad access.
std::optional<std::uint32_t> guest_word(std::uint8_t* rdram, std::uint32_t address);
std::optional<std::string> guest_string(std::uint8_t* rdram, std::uint32_t address,
                                        std::size_t maximum = 128);

// The game's menu text as it should be spoken: printable ASCII with runs of
// spaces collapsed. Text the game would still transform when drawing (a '~'
// placeholder) or bytes outside printable ASCII are not spoken at all.
std::optional<std::string> prepare_game_text(std::string_view text);

} // namespace dkr::accessibility
