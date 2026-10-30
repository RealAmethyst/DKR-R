#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::runtime::audio {

// The Sound page's output device setting. An empty name selects the system
// default. A saved device that is not connected plays on the default for the
// session without being forgotten, so it is used again once it is present.
constexpr std::size_t kMaximumOutputDeviceName = 512U;

// A name the settings file can hold on one line.
constexpr bool valid_output_device_name(std::string_view name) {
    return name.size() <= kMaximumOutputDeviceName &&
           std::none_of(name.begin(), name.end(), [](char value) {
               const auto byte = static_cast<unsigned char>(value);
               return byte < 0x20U || byte == 0x7FU;
           });
}

// The device to open: the saved device when it is connected, otherwise the
// system default (empty).
inline std::string output_device_to_open(std::string_view saved,
                                         const std::vector<std::string>& connected) {
    if (saved.empty() || !valid_output_device_name(saved)) {
        return {};
    }
    return std::find(connected.begin(), connected.end(), saved) != connected.end()
               ? std::string(saved)
               : std::string{};
}

} // namespace dkr::runtime::audio
