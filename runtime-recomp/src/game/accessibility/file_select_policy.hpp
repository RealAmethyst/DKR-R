#pragma once

#include "localization.hpp"
#include "menu_speech_policy.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace dkr::accessibility {

// A save slot as fileselect_render draws it: a started save shows its name,
// a balloon count and a balloon for its adventure (artwork); an empty one
// shows GAME A, B or C and NEW.
struct SaveSlot {
    std::string name;
    bool started = false;
    bool adventure_two = false;
    int balloons = 0;
};

struct FileSelectText {
    std::string heading;
    std::string new_save;
    std::string adventure_two;
    std::string copy;
    std::string erase;
};

// What File Select says. The save slots are one row of three; COPY and
// ERASE below them are a row of two.
class FileSelectNarrator {
public:
    // "ABC, ADVENTURE TWO, 12 BALLOONS, 1 of 3"; "GAME A, NEW, 1 of 3".
    static std::string slot(const FileSelectText& text, const std::array<SaveSlot, 3>& slots, std::size_t index) {
        if (index >= slots.size()) {
            return {};
        }
        const SaveSlot& save = slots[index];
        std::vector<std::string> parts;
        if (!save.name.empty()) {
            parts.push_back(save.name);
        }
        if (!save.started) {
            if (save.name.empty() || text.new_save.empty()) {
                return {};
            }
            parts.push_back(text.new_save);
        } else {
            if (save.adventure_two) {
                if (text.adventure_two.empty()) {
                    return {};
                }
                parts.push_back(text.adventure_two);
            }
            parts.push_back(text::balloons(save.balloons));
        }
        std::string name;
        for (const std::string& part : parts) {
            name += (name.empty() ? "" : ", ") + part;
        }
        return text::item(name, index + 1U, slots.size());
    }

    // The root screen's selection: a slot, COPY or ERASE (gMenuStage 0, 1, 2).
    static std::string root_item(const FileSelectText& text, const std::array<SaveSlot, 3>& slots, int stage,
                                 std::size_t index) {
        switch (stage) {
            case 0: return slot(text, slots, index);
            case 1: return text.copy.empty() ? std::string{} : text::item(text.copy, 1, 2);
            case 2: return text.erase.empty() ? std::string{} : text::item(text.erase, 2, 2);
            default: return {};
        }
    }

    // GAME SELECT, then the selection.
    static std::vector<Utterance> opened(const FileSelectText& text, const std::array<SaveSlot, 3>& slots,
                                         int stage, std::size_t index) {
        std::vector<Utterance> result;
        if (!text.heading.empty()) {
            result.push_back({text.heading, true});
        }
        append(result, root_item(text, slots, stage, index));
        return result;
    }

    // A prompt such as SELECT GAME TO COPY, then the highlighted slot.
    static std::vector<Utterance> prompt(const std::string& prompt, std::string slot) {
        std::vector<Utterance> result;
        append(result, prompt);
        append(result, std::move(slot));
        return result;
    }

    static std::vector<Utterance> say(std::string text) {
        std::vector<Utterance> result;
        append(result, std::move(text));
        return result;
    }

private:
    static void append(std::vector<Utterance>& result, std::string text) {
        if (!text.empty()) {
            result.push_back({std::move(text), result.empty()});
        }
    }
};

// Name entry (filename_render): ENTER YOUR INITIALS over a carousel of 31
// keys, the letters, '.' and '?', then SP, DEL and OK.
constexpr std::size_t kNameKeys = 31;
constexpr std::size_t kNameOkKey = 30;

class NameEntryNarrator {
public:
    static std::vector<Utterance> opened(const std::string& prompt, const MenuList& keys) {
        return MenuNarrator{}.opened(prompt, keys);
    }

    static std::vector<Utterance> cursor(const MenuList& keys) { return MenuNarrator{}.item_changed(keys); }

    // The name so far; with the name full, the cursor's jump to OK follows.
    static std::vector<Utterance> typed(const std::string& name, bool full, MenuList keys) {
        std::vector<Utterance> result;
        if (!name.empty()) {
            result.push_back({name, true});
        }
        if (full) {
            keys.selected = kNameOkKey;
            for (Utterance& item : MenuNarrator{}.item_changed(keys)) {
                item.interrupt = result.empty();
                result.push_back(std::move(item));
            }
        }
        return result;
    }

    // What is left of the name; nothing once it is empty.
    static std::vector<Utterance> deleted(const std::string& name) {
        if (name.empty()) {
            return {};
        }
        return {{name, true}};
    }
};

} // namespace dkr::accessibility
