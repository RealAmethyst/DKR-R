#pragma once

#include <atomic>
#include <cstdint>

// Counters for diagnosing the voice capture (DKR_SPATIAL_AUDIO_TRACE=1): a
// summary line every two seconds of output on the audio thread.
namespace dkr::runtime::spatial_audio::trace {

struct Counters {
    // SP task thread, per audio task with a plan.
    std::atomic<std::uint32_t> planned_tasks{0};
    std::atomic<std::uint32_t> planned_voices{0};
    std::atomic<std::uint32_t> kept_voices{0};
    std::atomic<std::uint32_t> reject_handle{0};
    std::atomic<std::uint32_t> reject_user_handle{0};
    std::atomic<std::uint32_t> reject_not_playing{0};
    std::atomic<std::uint32_t> reject_voice{0};
    std::atomic<std::uint32_t> rewrite_failed{0};
    std::atomic<std::uint32_t> captured_voices{0};
    std::atomic<std::uint32_t> submitted{0};
    std::atomic<std::uint32_t> no_plan_stale{0};
    // Audio thread, per output block.
    std::atomic<std::uint32_t> blocks{0};
    std::atomic<std::uint32_t> matched{0};
    std::atomic<std::uint32_t> unmatched{0};
    std::atomic<std::uint32_t> sources_begun{0};
    std::atomic<std::uint32_t> sources_finished{0};
    std::atomic<std::uint32_t> underruns{0};
};

bool enabled();
Counters& counters();
inline void count(std::atomic<std::uint32_t>& counter, std::uint32_t by = 1) {
    if (enabled()) counter.fetch_add(by, std::memory_order_relaxed);
}
// Audio thread: prints and clears the counters every `period` blocks.
void report(std::uint32_t period);

} // namespace dkr::runtime::spatial_audio::trace
