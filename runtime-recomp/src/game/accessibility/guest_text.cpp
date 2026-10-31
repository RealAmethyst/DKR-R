#include "guest_text.hpp"

namespace dkr::accessibility {

std::optional<std::string> prepare_game_text(std::string_view text) {
    std::string result;
    for (const char value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte < 0x20U || byte > 0x7EU || value == '~') {
            return std::nullopt;
        }
        if (value == ' ' && (result.empty() || result.back() == ' ')) {
            continue;
        }
        result += value;
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

} // namespace dkr::accessibility
