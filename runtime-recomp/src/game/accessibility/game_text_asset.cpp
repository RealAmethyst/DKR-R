#include "game_text_asset.hpp"

namespace dkr::accessibility {
namespace {

constexpr std::uint32_t kSubtitle = 0xFF000000U;
constexpr std::uint32_t kOffset = 0x00FFFFFFU;
// Text box bytes (game_text.c): a page break, the end, and the drawing
// commands func_800C38B4 reads, SetBorder (4) and SetColour (5) with four
// argument bytes, the others with one.
constexpr std::uint8_t kNextPage = 1;
constexpr std::uint8_t kEnd = 2;
constexpr std::uint8_t kFirstCommand = 3;
constexpr std::uint8_t kPastCommands = 13;
constexpr std::uint8_t kSetBorder = 4;
constexpr std::uint8_t kSetColour = 5;

std::uint32_t Word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | static_cast<std::uint32_t>(bytes[at + 3]);
}

bool Command(std::uint8_t value) {
    return value >= kFirstCommand && value < kPastCommands;
}

} // namespace

std::optional<std::span<const std::uint8_t>> game_textbox(std::span<const std::uint8_t> table,
                                                          std::span<const std::uint8_t> texts, std::uint32_t id,
                                                          std::uint32_t language) {
    // gTextTableEntries: the table's words less two; the id is checked
    // before the language moves it.
    const std::size_t words = table.size() / 4;
    if (words < 2 || id >= words - 2 || language >= kGameTextLanguages) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(id) + language * kGameTextsPerLanguage;
    if (index + 1 >= words) return std::nullopt;
    const std::uint32_t entry = Word(table, index * 4);
    const std::uint32_t next = Word(table, (index + 1) * 4);
    if ((entry & kSubtitle) != 0) return std::nullopt;
    const std::uint32_t begin = entry & kOffset;
    const std::uint32_t end = next & kOffset;
    if (end <= begin || end > texts.size() || end - begin >= kTextboxBuffer) return std::nullopt;
    return texts.subspan(begin, end - begin);
}

std::optional<std::vector<std::string>> textbox_first_page_lines(std::span<const std::uint8_t> textbox) {
    // set_current_text writes the end byte just past the text.
    const auto at = [&](std::size_t index) { return index < textbox.size() ? textbox[index] : kEnd; };
    std::size_t index = 0;
    // Skips a run of drawing commands; false when one runs past the text.
    const auto commands = [&]() {
        while (Command(at(index))) {
            const std::size_t length = at(index) == kSetBorder || at(index) == kSetColour ? 5 : 2;
            if (length > textbox.size() - index) return false;
            index += length;
        }
        return true;
    };
    std::vector<std::string> lines;
    if (!commands()) return std::nullopt;
    while (true) {
        // A command run straight before a page break or the end draws no
        // line of its own.
        if (at(index) == kNextPage || at(index) == kEnd) return lines;
        std::string line;
        while (index < textbox.size() && textbox[index] != 0) line += static_cast<char>(textbox[index++]);
        // A line without its 0 byte runs on past the text box.
        if (index >= textbox.size()) return std::nullopt;
        lines.push_back(std::move(line));
        while (at(index) == 0) ++index;
        if (at(index) < kFirstCommand) return lines;
        if (!commands()) return std::nullopt;
    }
}

} // namespace dkr::accessibility
