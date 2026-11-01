#include "spatial_audio/trace.hpp"

#include <cstdio>
#include <cstdlib>

namespace dkr::runtime::spatial_audio::trace {
namespace {

Counters g_counters;

std::uint32_t Take(std::atomic<std::uint32_t>& counter) {
    return counter.exchange(0, std::memory_order_relaxed);
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const char* value = std::getenv("DKR_SPATIAL_AUDIO_TRACE");
        return value != nullptr && value[0] == '1';
    }();
    return on;
}

Counters& counters() {
    return g_counters;
}

void report(std::uint32_t period) {
    if (!enabled() || g_counters.blocks.load(std::memory_order_relaxed) < period) return;
    Counters& c = g_counters;
    const std::uint32_t blocks = Take(c.blocks);
    const std::uint32_t matched = Take(c.matched);
    const std::uint32_t unmatched = Take(c.unmatched);
    const std::uint32_t begun = Take(c.sources_begun);
    const std::uint32_t finished = Take(c.sources_finished);
    const std::uint32_t underruns = Take(c.underruns);
    const std::uint32_t tasks = Take(c.planned_tasks);
    const std::uint32_t planned = Take(c.planned_voices);
    const std::uint32_t kept = Take(c.kept_voices);
    const std::uint32_t handle = Take(c.reject_handle);
    const std::uint32_t user_handle = Take(c.reject_user_handle);
    const std::uint32_t not_playing = Take(c.reject_not_playing);
    const std::uint32_t voice = Take(c.reject_voice);
    const std::uint32_t failed = Take(c.rewrite_failed);
    const std::uint32_t captured = Take(c.captured_voices);
    const std::uint32_t submitted = Take(c.submitted);
    const std::uint32_t stale = Take(c.no_plan_stale);
    std::fprintf(stderr,
                 "[trace][spatial-audio] blocks=%u matched=%u unmatched=%u begun=%u finished=%u "
                 "underruns=%u | tasks=%u planned=%u kept=%u reject(handle=%u user=%u "
                 "not_playing=%u voice=%u) rewrite_failed=%u captured=%u submitted=%u stale=%u\n",
                 blocks, matched, unmatched, begun, finished, underruns, tasks, planned, kept, handle,
                 user_handle, not_playing, voice, failed, captured, submitted, stale);
}

} // namespace dkr::runtime::spatial_audio::trace
