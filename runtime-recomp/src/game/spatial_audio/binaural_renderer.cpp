#include "spatial_audio/binaural_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dkr::runtime::spatial_audio {
namespace {

// The rate Steam Audio renders at for an output rate, or 0 when unsupported.
std::uint32_t ProcessingRateFor(std::uint32_t output_rate) {
    switch (output_rate) {
    case 22050: return 44100;
    case 24000: return 48000;
    case 44100:
    case 48000: return output_rate;
    default: return 0;
    }
}

void IPLCALL Log(IPLLogLevel level, const char* message) {
    if (level == IPL_LOGLEVEL_DEBUG) return;
    std::fprintf(stderr, "[boot][spatial-audio] steam audio: %s\n", message);
}

// Steam Audio divides by the direction's length; a sound exactly at the
// listener is placed ahead instead.
IPLVector3 Normalised(const Direction& direction) {
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                   direction.z * direction.z);
    if (!(length > 1.0e-6F)) return IPLVector3{0.0F, 0.0F, -1.0F};
    return IPLVector3{direction.x / length, direction.y / length, direction.z / length};
}

} // namespace

Renderer::~Renderer() {
    stop();
}

bool Renderer::start(std::uint32_t output_rate) {
    if (running() && output_rate == output_rate_) return true;
    stop();
    const std::uint32_t processing_rate = ProcessingRateFor(output_rate);
    if (processing_rate == 0) {
        std::fprintf(stderr, "[boot][spatial-audio] off: output rate %u Hz is not supported\n",
                     output_rate);
        return false;
    }

    IPLContextSettings context_settings{};
    context_settings.version = STEAMAUDIO_VERSION;
    context_settings.logCallback = Log;
    if (iplContextCreate(&context_settings, &context_) != IPL_STATUS_SUCCESS) {
        std::fprintf(stderr, "[boot][spatial-audio] off: context creation failed\n");
        context_ = nullptr;
        return false;
    }

    IPLAudioSettings audio{static_cast<IPLint32>(processing_rate),
                           static_cast<IPLint32>(kFrameSize)};
    // A zeroed volume renders silence, so it is set explicitly.
    IPLHRTFSettings hrtf_settings{};
    hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
    hrtf_settings.volume = 1.0F;
    if (iplHRTFCreate(context_, &audio, &hrtf_settings, &hrtf_) != IPL_STATUS_SUCCESS) {
        std::fprintf(stderr, "[boot][spatial-audio] off: HRTF creation failed\n");
        hrtf_ = nullptr;
        stop();
        return false;
    }
    if (iplAudioBufferAllocate(context_, 1, audio.frameSize, &mono_) != IPL_STATUS_SUCCESS ||
        iplAudioBufferAllocate(context_, 2, audio.frameSize, &voice_out_) != IPL_STATUS_SUCCESS) {
        std::fprintf(stderr, "[boot][spatial-audio] off: buffer allocation failed\n");
        stop();
        return false;
    }
    IPLBinauralEffectSettings effect_settings{hrtf_};
    for (Voice& voice : voices_) {
        if (iplBinauralEffectCreate(context_, &audio, &effect_settings, &voice.effect) !=
            IPL_STATUS_SUCCESS) {
            std::fprintf(stderr, "[boot][spatial-audio] off: binaural effect creation failed\n");
            voice.effect = nullptr;
            stop();
            return false;
        }
    }

    output_rate_ = output_rate;
    processing_rate_ = processing_rate;
    output_frames_ = 0;
    output_read_ = 0;
    decimator_.reset();
    std::fprintf(stderr,
                 "[boot][spatial-audio] binaural renderer ready output=%u processing=%u frame=%zu\n",
                 output_rate_, processing_rate_, kFrameSize);
    return true;
}

void Renderer::stop() {
    for (Voice& voice : voices_) {
        if (voice.effect != nullptr) iplBinauralEffectRelease(&voice.effect);
        voice = Voice{};
    }
    if (context_ != nullptr) {
        if (voice_out_.data != nullptr) iplAudioBufferFree(context_, &voice_out_);
        if (mono_.data != nullptr) iplAudioBufferFree(context_, &mono_);
    }
    voice_out_ = IPLAudioBuffer{};
    mono_ = IPLAudioBuffer{};
    if (hrtf_ != nullptr) iplHRTFRelease(&hrtf_);
    if (context_ != nullptr) iplContextRelease(&context_);
    hrtf_ = nullptr;
    context_ = nullptr;
    output_rate_ = 0;
    processing_rate_ = 0;
    output_frames_ = 0;
    output_read_ = 0;
}

bool Renderer::play(Source& source) {
    if (!running() || playing(source)) return false;
    for (Voice& voice : voices_) {
        if (voice.state == VoiceState::Idle) {
            voice.source = &source;
            voice.state = VoiceState::Playing;
            return true;
        }
    }
    std::fprintf(stderr, "[boot][spatial-audio] no free voice\n");
    return false;
}

bool Renderer::playing(const Source& source) const {
    for (const Voice& voice : voices_) {
        if (voice.state != VoiceState::Idle && voice.source == &source) return true;
    }
    return false;
}

bool Renderer::active() const {
    if (!running()) return false;
    if (output_read_ < output_frames_) return true;
    for (const Voice& voice : voices_) {
        if (voice.state != VoiceState::Idle) return true;
    }
    return false;
}

void Renderer::render_block() {
    block_.fill(0.0F);
    for (Voice& voice : voices_) {
        if (voice.state == VoiceState::Idle) continue;
        IPLAudioEffectState state = IPL_AUDIOEFFECTSTATE_TAILCOMPLETE;
        if (voice.state == VoiceState::Playing) {
            Direction direction;
            if (voice.source->render(mono_.data[0], kFrameSize, direction)) {
                IPLBinauralEffectParams params{};
                params.direction = Normalised(direction);
                params.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
                params.spatialBlend = 1.0F;
                params.hrtf = hrtf_;
                iplBinauralEffectApply(voice.effect, &params, &mono_, &voice_out_);
                state = IPL_AUDIOEFFECTSTATE_TAILREMAINING;
            } else {
                voice.state = VoiceState::Tail;
            }
        }
        if (voice.state == VoiceState::Tail) {
            state = iplBinauralEffectGetTail(voice.effect, &voice_out_);
        }
        for (std::size_t frame = 0; frame < kFrameSize; ++frame) {
            block_[frame * 2] += voice_out_.data[0][frame];
            block_[frame * 2 + 1] += voice_out_.data[1][frame];
        }
        if (voice.state == VoiceState::Tail && state == IPL_AUDIOEFFECTSTATE_TAILCOMPLETE) {
            iplBinauralEffectReset(voice.effect);
            voice.source = nullptr;
            voice.state = VoiceState::Idle;
        }
    }
    if (processing_rate_ == output_rate_) {
        output_ = block_;
        output_frames_ = kFrameSize;
    } else {
        decimator_.process(block_.data(), output_.data(), kFrameSize / 2);
        output_frames_ = kFrameSize / 2;
    }
    output_read_ = 0;
}

void Renderer::mix_into(float* stereo, std::size_t frames) {
    if (!active()) return;
    std::size_t written = 0;
    while (written < frames) {
        if (output_read_ == output_frames_) {
            render_block();
        }
        const std::size_t count = std::min(frames - written, output_frames_ - output_read_);
        for (std::size_t frame = 0; frame < count; ++frame) {
            stereo[(written + frame) * 2] += output_[(output_read_ + frame) * 2];
            stereo[(written + frame) * 2 + 1] += output_[(output_read_ + frame) * 2 + 1];
        }
        written += count;
        output_read_ += count;
    }
}

} // namespace dkr::runtime::spatial_audio
