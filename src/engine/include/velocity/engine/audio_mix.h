#pragma once
// Shared timeline audio mixer (docs/07): resolves segments, decodes, applies
// gain + linear fade envelopes, and sums into interleaved float32 stereo at
// the engine rate. Used identically by export and playback — one mixing
// implementation means what you hear is what you export.
//
// Not thread-safe; each consumer owns one AudioMixer (it caches decoders).

#include <velocity/engine/compile.h>
#include <velocity/engine/model.h>

#include <map>
#include <memory>
#include <vector>

namespace velocity::media {
class AudioDecoder;
}

namespace velocity::engine {

class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    // Mixes [pos, pos+frames) of the sequence into out[frames*2], REPLACING
    // its contents (gaps become silence). masterGain applies after summing.
    // Never fails: undecodable assets contribute silence.
    void mix(const Sequence& seq, Tick pos, int frames, float* out, float masterGain = 1.0f);

    // Drops cached decoders (call when the snapshot's assets changed).
    void reset();

private:
    std::map<std::filesystem::path, std::unique_ptr<media::AudioDecoder>> decoders_;
    std::vector<float> segBuf_;
};

} // namespace velocity::engine
