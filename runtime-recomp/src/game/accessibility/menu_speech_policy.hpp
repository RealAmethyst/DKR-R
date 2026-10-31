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

// What the boot, title and Options screens say, following the project's
// speech rules: a displayed heading interrupts once per screen opening and
// its selected item is queued after it; with no heading the item interrupts;
// moving or changing an item speaks that item alone; items carry their
// position from the actual list. Returning to Options from one of its own
// sub-screens speaks the current item without the heading again.
class MenuNarrator {
public:
    std::vector<Utterance> screen_opened(MenuId id, const std::optional<std::string>& heading,
                                         const std::optional<MenuList>& list) {
        const std::optional<MenuId> previous = current_;
        current_ = id;
        if (id != MenuId::Options || !list) {
            return {};
        }
        const bool from_sub_screen = previous == MenuId::AudioOptions || previous == MenuId::SaveOptions ||
                                     previous == MenuId::MagicCodes || previous == MenuId::MagicCodesList;
        if (from_sub_screen || !heading) {
            return selected(*list, true);
        }
        std::vector<Utterance> result{{*heading, true}};
        for (Utterance& item : selected(*list, false)) {
            result.push_back(std::move(item));
        }
        return result;
    }

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

    std::optional<MenuId> current_;
};

} // namespace dkr::accessibility
