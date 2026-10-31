#include "game_text.hpp"

#include "symbol_lookup.hpp"
#include "guest_text.hpp"

#include <cstdio>

namespace dkr::accessibility {

std::optional<std::string> pointed_text(std::uint8_t* rdram, std::uint32_t pointer) {
    if (pointer == 0U) {
        return std::nullopt;
    }
    const std::optional<std::string> raw = guest_string(rdram, pointer);
    return raw ? prepare_game_text(*raw) : std::nullopt;
}

std::optional<std::int32_t> symbol_s32(std::uint8_t* rdram, const char* name) {
    const std::optional<std::uint32_t> address = symbols::data(name, 4U);
    const std::optional<std::uint32_t> value = address ? guest_word(rdram, *address) : std::nullopt;
    if (!value) {
        std::fprintf(stderr, "[speech][menu] unreadable: %s\n", name);
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*value);
}

std::optional<std::string> menu_text(std::uint8_t* rdram, std::uint32_t index) {
    const std::optional<std::uint32_t> table_symbol = symbols::data("gMenuText", 4U);
    const std::optional<std::uint32_t> table = table_symbol ? guest_word(rdram, *table_symbol) : std::nullopt;
    if (!table || index > 0x1000U) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> pointer = guest_word(rdram, *table + index * 4U);
    return pointer ? pointed_text(rdram, *pointer) : std::nullopt;
}

std::optional<std::string> string_array_entry(std::uint8_t* rdram, const char* array, std::uint32_t slot) {
    const std::optional<std::uint32_t> base = symbols::data(array, (slot + 1U) * 4U);
    const std::optional<std::uint32_t> pointer = base ? guest_word(rdram, *base + slot * 4U) : std::nullopt;
    return pointer ? pointed_text(rdram, *pointer) : std::nullopt;
}

} // namespace dkr::accessibility
