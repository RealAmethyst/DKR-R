#include "audio_output_policy.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    using namespace dkr::runtime::audio;
    static_assert(valid_output_device_name(""));
    static_assert(valid_output_device_name("RODECaster Pro II Line 4"));
    static_assert(!valid_output_device_name("Line 4\nmaster_volume=0"));
    static_assert(!valid_output_device_name("Line 4\r"));
    assert(valid_output_device_name(std::string(kMaximumOutputDeviceName, 'a')));
    assert(!valid_output_device_name(std::string(kMaximumOutputDeviceName + 1U, 'a')));

    const std::vector<std::string> connected{"Speakers", "Headphones"};
    // System default stays the default.
    assert(output_device_to_open("", connected).empty());
    // A connected saved device is opened by name.
    assert(output_device_to_open("Headphones", connected) == "Headphones");
    // A missing saved device plays on the default; the caller keeps the name.
    assert(output_device_to_open("USB Headset", connected).empty());
    assert(output_device_to_open("Headphones", {}).empty());
    // Names match exactly, never by prefix or case.
    assert(output_device_to_open("headphones", connected).empty());
    assert(output_device_to_open("Head", connected).empty());
    // A name the settings file could not hold is never opened.
    assert(output_device_to_open("Head\nphones", {"Head\nphones"}).empty());
    std::puts("[test][audio-output-policy] PASS");
    return 0;
}
