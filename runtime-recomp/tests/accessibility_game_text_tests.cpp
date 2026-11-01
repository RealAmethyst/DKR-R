#include "accessibility/game_text_asset.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace dkr::accessibility;
using namespace std::string_view_literals;

namespace {

using Bytes = std::vector<std::uint8_t>;
using Lines = std::vector<std::string>;

Bytes bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

void put_word(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

// A synthetic text section and its table: every language's texts in turn,
// kGameTextsPerLanguage apart, then the end offset and one more word (the
// table holds two words more than the game's text count).
struct Section {
    Bytes table;
    Bytes texts;
};

Section section(const std::vector<std::vector<Bytes>>& languages, std::uint32_t subtitle_index = 0xFFFFFFFFU) {
    Section result;
    std::uint32_t index = 0;
    for (const std::vector<Bytes>& language : languages) {
        for (std::uint32_t id = 0; id < kGameTextsPerLanguage; ++id, ++index) {
            const std::uint32_t flag = index == subtitle_index ? 0x80000000U : 0U;
            put_word(result.table, flag | static_cast<std::uint32_t>(result.texts.size()));
            if (id < language.size()) {
                result.texts.insert(result.texts.end(), language[id].begin(), language[id].end());
            }
        }
    }
    put_word(result.table, static_cast<std::uint32_t>(result.texts.size()));
    put_word(result.table, 0U);
    return result;
}

std::optional<Lines> lines(std::string_view text) {
    const Bytes record = bytes(text);
    return textbox_first_page_lines(record);
}

void first_page_lines_follow_the_drawing_commands() {
    // SetBorder (4 arguments), SetAlignment, SetFont, SetTimer, a line,
    // AddVerticalSpacing, two lines, then a page break.
    assert((lines("\x04\x28\x1E\x8C\xC8\x06\x00\x03\x00\x0A\x08HEADING\0\x08\x0A"
                  "FIRST LINE\0SECOND LINE\0\0"
                  "\x01NEXT PAGE\0"sv) == Lines{"HEADING", "FIRST LINE", "SECOND LINE"}));
    // SetColour's four arguments may be any byte, including 0, 1 and 2.
    assert((lines("\x05\x00\x01\x02\xFF\x0C\x01ONLY\0"sv) == Lines{"ONLY"}));
    // Ends at the end of the text, where set_current_text writes 2.
    assert((lines("LAST\0\0\0"sv) == Lines{"LAST"}));
    // A command run straight before the end draws no line.
    assert((lines("LINE\0\x0A\x05"sv) == Lines{"LINE"}));
    // A command run before a 0 draws an empty line, as the game would.
    assert((lines("\x06\x01\0LINE\0"sv) == Lines{"", "LINE"}));
}

void broken_text_boxes_are_withheld() {
    // A line without its 0 byte.
    assert(!lines("UNFINISHED"sv));
    // A command whose argument is past the end.
    assert(!lines("LINE\0\x04\x01\x02"sv));
    assert(!lines("\x03"sv));
}

void texts_are_found_per_language() {
    const Section texts = section({
        {bytes("ENGLISH ZERO\0"sv), bytes("\x06\x00" "ENGLISH ONE\0"sv)},
        {bytes("GERMAN ZERO\0"sv), bytes("GERMAN ONE\0"sv)},
        {bytes("FRENCH ZERO\0"sv), bytes("FRENCH ONE\0"sv)},
    });
    const auto first = [&](std::uint32_t id, std::uint32_t language) -> std::optional<Lines> {
        const auto record = game_textbox(texts.table, texts.texts, id, language);
        return record ? textbox_first_page_lines(*record) : std::nullopt;
    };
    assert((first(1, 0) == Lines{"ENGLISH ONE"}));
    assert((first(0, 1) == Lines{"GERMAN ZERO"}));
    assert((first(1, 2) == Lines{"FRENCH ONE"}));
    // An empty entry is no text box.
    assert(!game_textbox(texts.table, texts.texts, 2, 0));
    // No fourth language in this table, and no fifth language at all.
    assert(!game_textbox(texts.table, texts.texts, 0, 3));
    assert(!game_textbox(texts.table, texts.texts, 0, 4));
    // Past the game's text count (the table's words less two).
    assert(!game_textbox(texts.table, texts.texts, static_cast<std::uint32_t>(texts.table.size() / 4 - 2), 0));
}

void subtitles_and_bad_tables_are_refused() {
    // The negative control: the same text flagged as a subtitle is refused.
    const std::vector<std::vector<Bytes>> one = {{bytes("TEXT\0"sv)}};
    const Section plain = section(one);
    assert(game_textbox(plain.table, plain.texts, 0, 0));
    const Section subtitle = section(one, 0);
    assert(!game_textbox(subtitle.table, subtitle.texts, 0, 0));
    // An offset past the text section.
    Section cut = plain;
    cut.texts.pop_back();
    assert(!game_textbox(cut.table, cut.texts, 0, 0));
    // Larger than a text box buffer with its end byte.
    const Section large = section({{Bytes(kTextboxBuffer, 'A')}});
    assert(!game_textbox(large.table, large.texts, 0, 0));
    const Section fits = section({{Bytes(kTextboxBuffer - 1, 'A')}});
    assert(game_textbox(fits.table, fits.texts, 0, 0));
    // Too short to hold a text.
    assert(!game_textbox(std::span<const std::uint8_t>(plain.table).first(4), plain.texts, 0, 0));
}

} // namespace

int main() {
    first_page_lines_follow_the_drawing_commands();
    broken_text_boxes_are_withheld();
    texts_are_found_per_language();
    subtitles_and_bad_tables_are_refused();
    std::puts("[test][accessibility-game-text] PASS");
    return 0;
}
