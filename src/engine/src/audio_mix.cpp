#include "velocity/engine/audio_mix.h"

#include <velocity/media/audio_decoder.h>

#include <algorithm>
#include <cstring>

namespace velocity::engine {

AudioMixer::AudioMixer() = default;
AudioMixer::~AudioMixer() = default;

void AudioMixer::reset() { decoders_.clear(); }

namespace {
// Linear fade envelope at absolute timeline tick t for a segment's clip.
float envelopeAt(const AudioSegment& seg, Tick t) {
    float env = 1.0f;
    if (seg.fadeIn > 0 && t < seg.clipStart + seg.fadeIn)
        env *= static_cast<float>(t - seg.clipStart) / static_cast<float>(seg.fadeIn);
    if (seg.fadeOut > 0 && t > seg.clipEnd - seg.fadeOut)
        env *= static_cast<float>(seg.clipEnd - t) / static_cast<float>(seg.fadeOut);
    return std::clamp(env, 0.0f, 1.0f);
}
} // namespace

void AudioMixer::mix(const Sequence& seq, Tick pos, int frames, float* out, float masterGain) {
    std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(float));
    if (frames <= 0)
        return;

    for (const auto& seg : audioSegmentsInRange(seq, pos, frames)) {
        auto it = decoders_.find(seg.asset);
        if (it == decoders_.end()) {
            auto dec = media::AudioDecoder::open(seg.asset);
            if (!dec) {
                decoders_.emplace(seg.asset, nullptr); // remember failures too
                continue;
            }
            it = decoders_.emplace(seg.asset, std::move(dec.value())).first;
        }
        if (!it->second)
            continue;

        const Tick pieceStart = std::max(seg.start, pos);
        const Tick pieceLen = std::min(seg.start + seg.len, pos + frames) - pieceStart;
        if (pieceLen <= 0)
            continue;

        const std::int64_t srcStart = ticksFromPts(seg.srcStartPts, seg.srcTimebase) +
                                      (pieceStart - seg.start);
        segBuf_.assign(static_cast<size_t>(pieceLen) * 2, 0.0f);
        if (auto r = it->second->readAt(srcStart, segBuf_.data(), static_cast<int>(pieceLen));
            !r)
            continue;

        float* dst = out + static_cast<size_t>(pieceStart - pos) * 2;
        // Sample the fade envelope per 64-sample run: inaudible steps at
        // 48 kHz, ~64x cheaper than per-sample evaluation.
        for (Tick i = 0; i < pieceLen; i += 64) {
            const Tick runLen = std::min<Tick>(64, pieceLen - i);
            const float g = seg.gain * envelopeAt(seg, pieceStart + i);
            for (Tick j = i * 2; j < (i + runLen) * 2; ++j)
                dst[j] += segBuf_[static_cast<size_t>(j)] * g;
        }
    }

    for (int i = 0; i < frames * 2; ++i)
        out[i] = std::clamp(out[i] * masterGain, -1.0f, 1.0f);
}

} // namespace velocity::engine
