#include "spatial_audio/voice_capture.hpp"

#include "spatial_audio/trace.hpp"

#include <algorithm>

namespace dkr::runtime::spatial_audio {

void CaptureSource::begin(std::uint32_t key) {
    key_ = key;
    finishing_ = false;
    interpolator_.reset();
    read_ = 0;
    std::fill_n(fifo_.begin(), Renderer::kFrameSize, 0.0F);
    size_ = Renderer::kFrameSize;
}

void CaptureSource::push(const float* mono, std::size_t frames, const Direction& direction) {
    direction_ = direction;
    frames = std::min(frames, kMaxFrameSamples);
    interpolator_.process(mono, doubled_.data(), frames);
    for (std::size_t index = 0; index < frames * 2; ++index) {
        // A full queue drops the oldest audio rather than growing latency.
        if (size_ == kCapacity) {
            read_ = (read_ + 1) % kCapacity;
            --size_;
        }
        fifo_[(read_ + size_) % kCapacity] = doubled_[index];
        ++size_;
    }
}

bool CaptureSource::render(float* mono, std::size_t frames, Direction& direction) {
    if (finishing_ && size_ == 0) return false;
    direction = direction_;
    const std::size_t available = std::min(frames, size_);
    for (std::size_t index = 0; index < available; ++index) {
        mono[index] = fifo_[read_];
        read_ = (read_ + 1) % kCapacity;
    }
    size_ -= available;
    if (available < frames && !finishing_) trace::count(trace::counters().underruns);
    std::fill(mono + available, mono + frames, 0.0F);
    return true;
}

} // namespace dkr::runtime::spatial_audio
