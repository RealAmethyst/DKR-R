#include "game_symbols.hpp"

#include "revision_addresses.hpp"

#include <span>

namespace dkr::accessibility::symbols {
namespace {

struct Symbol {
    const char* name;
    std::uint32_t address;
    std::uint32_t size;
    bool function;
};

constexpr Symbol kSymbolsV77[] = {
#include "../../../../modding/reference-us-v77/game_symbols.inc"
};
constexpr Symbol kSymbolsV80[] = {
#include "../../../../modding/reference-us-v80/game_symbols.inc"
};

} // namespace

std::optional<std::uint32_t> data(std::string_view name, std::uint32_t minimum_size) {
    const std::span<const Symbol> table =
        runtime::revision_addresses::selected_revision() == runtime::rom::Revision::UsV80
            ? std::span<const Symbol>(kSymbolsV80)
            : std::span<const Symbol>(kSymbolsV77);
    std::optional<std::uint32_t> found;
    for (const Symbol& symbol : table) {
        if (symbol.function || name != symbol.name) {
            continue;
        }
        if (found || symbol.size < minimum_size) {
            return std::nullopt;
        }
        found = symbol.address;
    }
    return found;
}

} // namespace dkr::accessibility::symbols
