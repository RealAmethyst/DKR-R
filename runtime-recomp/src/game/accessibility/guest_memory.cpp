#include "guest_text.hpp"

#include "recomp.h"

namespace dkr::accessibility {
namespace {
gpr guest_register(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
} // namespace

std::optional<std::uint32_t> guest_word(std::uint8_t* rdram, std::uint32_t address) {
    if (rdram == nullptr || address % 4U != 0U || !guest_readable(address, 4U)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(MEM_W(0, guest_register(address)));
}

std::optional<std::int16_t> guest_half(std::uint8_t* rdram, std::uint32_t address) {
    if (rdram == nullptr || address % 2U != 0U || !guest_readable(address, 2U)) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(MEM_H(0, guest_register(address)));
}

std::optional<std::int8_t> guest_byte(std::uint8_t* rdram, std::uint32_t address) {
    if (rdram == nullptr || !guest_readable(address, 1U)) {
        return std::nullopt;
    }
    return static_cast<std::int8_t>(MEM_B(0, guest_register(address)));
}

std::optional<std::string> guest_string(std::uint8_t* rdram, std::uint32_t address, std::size_t maximum) {
    if (rdram == nullptr || !guest_readable(address, 1U)) {
        return std::nullopt;
    }
    std::string result;
    for (std::size_t index = 0; index <= maximum; ++index) {
        const std::uint32_t at = address + static_cast<std::uint32_t>(index);
        if (!guest_readable(at, 1U)) {
            return std::nullopt;
        }
        const char value = static_cast<char>(MEM_B(0, guest_register(at)));
        if (value == '\0') {
            return result;
        }
        result += value;
    }
    // Unterminated within the limit: not a string the game draws.
    return std::nullopt;
}

} // namespace dkr::accessibility
