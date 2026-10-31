#pragma once

#include "localization.hpp"
#include "menu_events.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace dkr::accessibility {

struct Utterance {
    std::string text;
    bool interrupt = false;

    bool operator==(const Utterance&) const = default;
};

// A navigable list as the screen draws it. An entry the text pipeline could
// not verify is empty: it still counts for positions but is never spoken.
struct MenuList {
    std::vector<std::string> items;
    std::size_t selected = 0;
};

// What the stock screens say, following the project's speech rules: with no
// displayed heading the selected item interrupts; moving an item speaks that
// item alone; items carry their position from the actual list. The title
// menu appears on reveal. The redesigned Options menu speaks for itself.
class MenuNarrator {
public:
    // The title menu has no displayed heading; it appears on reveal.
    std::vector<Utterance> title_revealed(const MenuList& list) { return selected(list, true); }

    std::vector<Utterance> item_changed(const MenuList& list) { return selected(list, true); }

private:
    static std::vector<Utterance> selected(const MenuList& list, bool interrupt) {
        if (list.selected >= list.items.size() || list.items[list.selected].empty()) {
            return {};
        }
        return {{text::item(list.items[list.selected], list.selected + 1U, list.items.size()), interrupt}};
    }
};

} // namespace dkr::accessibility
