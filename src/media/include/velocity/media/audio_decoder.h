#pragma once
// Sample-accurate audio decoding (docs/04, docs/07): any supported source →
// interleaved float32 at the engine rate/channel count. Positions are in
// output samples (48 kHz == engine ticks), so the audio graph and the
// timeline share one time domain.

#include <velocity/foundation/expected.h>
#include <velocity/media/error.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace velocity::media {

class AudioDecoder {
public:
    static Expected<std::unique_ptr<AudioDecoder>, MediaError>
    open(const std::filesystem::path& file, int targetRate = 48000, int targetChannels = 2);
    ~AudioDecoder();

    // Fills out[frames*channels] with samples starting at absolute output
    // sample position `pos`. Regions past EOF (or before 0) are zero-filled.
    Expected<void, MediaError> readAt(std::int64_t pos, float* out, int frames);

    // Best-known stream length in output samples (0 when unknown).
    [[nodiscard]] std::int64_t lengthSamples() const;

    [[nodiscard]] int rate() const;
    [[nodiscard]] int channels() const;

    struct Impl;

private:
    AudioDecoder() = default;
    std::unique_ptr<Impl> impl_;
};

} // namespace velocity::media
