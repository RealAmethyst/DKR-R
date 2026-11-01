#include "game_text.hpp"

#include "game_text_asset.hpp"
#include "symbol_lookup.hpp"
#include "guest_text.hpp"
#include "mods/legacy_mod_format.hpp"
#include "revision_addresses.hpp"

#include <librecomp/game.hpp>

#include <cstdio>
#include <exception>
#include <map>
#include <tuple>
#include <vector>

namespace dkr::accessibility {
namespace {

namespace rom = runtime::rom;

// Asset sections (AssetSectionsEnum, asset_enums.h; the same in both
// revisions).
constexpr unsigned kGameTextSection = 6;      // ASSET_GAME_TEXT
constexpr unsigned kGameTextTableSection = 7; // ASSET_GAME_TEXT_TABLE

// get_language (menu.c): sEepromSettings & 0xC is 0x4 German, 0x8 French,
// 0xC Japanese, else English; Language (enums.h) counts them 0 to 3.
// sEepromSettings is a u64; the language bits sit in its low word.
std::optional<std::uint32_t> Language(std::uint8_t* rdram) {
    const std::optional<std::uint32_t> eeprom_symbol = symbols::data("sEepromSettings", 8U);
    const std::optional<std::uint32_t> eeprom = eeprom_symbol ? guest_word(rdram, *eeprom_symbol + 4U) : std::nullopt;
    if (!eeprom) {
        return std::nullopt;
    }
    return (*eeprom & 0xCU) >> 2U;
}

// The first page's raw lines of a game text, from the asset directory of the
// ROM image librecomp's PI DMA reads (recomp::do_rom_read), located by the
// legacy mod format's reviewed adapter for the revision.
std::optional<std::vector<std::string>> ReadTextboxLines(rom::Revision revision, std::uint32_t language,
                                                         std::uint32_t id) {
    const char* adapter = revision == rom::Revision::UsV77   ? "us.v77"
                          : revision == rom::Revision::UsV80 ? "us.v80"
                                                             : nullptr;
    if (adapter == nullptr) {
        std::fprintf(stderr, "[speech][game-text] text %u: no supported revision selected\n", id);
        return std::nullopt;
    }
    std::optional<std::span<const std::uint8_t>> textbox;
    try {
        const mods::AssetImage image(recomp::get_rom(), adapter);
        textbox = game_textbox(image.sections[kGameTextTableSection], image.sections[kGameTextSection], id, language);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[speech][game-text] text %u: asset directory unreadable: %s\n", id, error.what());
        return std::nullopt;
    }
    if (!textbox) {
        std::fprintf(stderr, "[speech][game-text] text %u language %u: not a text box in the table\n", id, language);
        return std::nullopt;
    }
    std::optional<std::vector<std::string>> lines = textbox_first_page_lines(*textbox);
    if (!lines) {
        std::fprintf(stderr, "[speech][game-text] text %u language %u: first page does not decode\n", id, language);
    }
    return lines;
}

} // namespace

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

std::optional<std::string> level_name(std::uint8_t* rdram, std::int32_t level) {
    const std::optional<std::uint32_t> names_symbol = symbols::data("gLevelNames", 4U);
    const std::optional<std::uint32_t> count_symbol = symbols::data("gNumberOfLevelHeaders", 4U);
    if (!names_symbol || !count_symbol) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> names = guest_word(rdram, *names_symbol);
    const std::optional<std::uint32_t> count = guest_word(rdram, *count_symbol);
    const std::optional<std::uint32_t> language = Language(rdram);
    if (!names || !count || !language || level < 0 || static_cast<std::uint32_t>(level) >= *count) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> pointer = guest_word(rdram, *names + static_cast<std::uint32_t>(level) * 4U);
    if (!pointer) {
        return std::nullopt;
    }
    // The names follow in Language order, each ending in a zero byte.
    std::uint32_t at = *pointer;
    for (std::uint32_t passed = 0; passed < *language;) {
        const std::optional<std::string> name = guest_string(rdram, at);
        if (!name) {
            return std::nullopt;
        }
        at += static_cast<std::uint32_t>(name->size()) + 1U;
        ++passed;
    }
    return pointed_text(rdram, at);
}

std::optional<std::string> game_textbox_line(std::uint8_t* rdram, std::uint32_t id, std::size_t line) {
    // Failures are kept too, so an unreadable text is decoded and logged once.
    static std::map<std::tuple<rom::Revision, std::uint32_t, std::uint32_t>, std::optional<std::vector<std::string>>>
        cache;
    const std::optional<std::uint32_t> language = Language(rdram);
    if (!language) {
        return std::nullopt;
    }
    const rom::Revision revision = runtime::revision_addresses::selected_revision();
    const auto key = std::make_tuple(revision, *language, id);
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(key, ReadTextboxLines(revision, *language, id)).first;
    }
    if (!found->second || line >= found->second->size()) {
        return std::nullopt;
    }
    return prepare_game_text((*found->second)[line]);
}

} // namespace dkr::accessibility
