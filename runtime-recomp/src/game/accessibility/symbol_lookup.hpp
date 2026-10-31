#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace dkr::accessibility::symbols {

// A data symbol of the selected ROM revision, looked up by exact name in the
// named tables generated from the decomp ELF (modding/reference-us-v*/). The
// symbol must be unique and at least minimum_size bytes; otherwise nullopt,
// and the caller speaks nothing.
std::optional<std::uint32_t> data(std::string_view name, std::uint32_t minimum_size);

} // namespace dkr::accessibility::symbols
