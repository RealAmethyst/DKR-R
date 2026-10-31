#include "localization.hpp"

namespace dkr::accessibility::text {

std::string position(std::size_t index, std::size_t total) {
    return std::to_string(index) + " of " + std::to_string(total);
}

std::string item(std::string_view name, std::size_t index, std::size_t total) {
    return std::string(name) + ", " + position(index, total);
}

} // namespace dkr::accessibility::text
