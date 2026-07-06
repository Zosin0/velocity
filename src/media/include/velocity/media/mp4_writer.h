#pragma once
// Phase-2 export sink (docs/10, reduced to the single primary path):
// MP4 container, H.264 video (hardware encoder preferred, libopenh264
// fallback), AAC audio. Input: CPU video frames of any supported size/format
// (converted/scaled internally) and interleaved float32 audio.

#include <velocity/foundation/expected.h>
#include <velocity/foundation/time.h>
#include <velocity/media/error.h>
#include <velocity/media/video_decoder.h>

#include <filesystem>
#include <memory>
#include <string>

namespace velocity::media {

struct ExportFormat {
    int width = 1920;
    int height = 1080;
    Rational fps{30, 1};
    int audioRate = 48000;
    int audioChannels = 2;
    std::int64_t videoBitrate = 8'000'000;
    std::int64_t audioBitrate = 192'000;
    bool preferHardwareEncoder = true;
};

class Mp4Writer {
public:
    static Expected<std::unique_ptr<Mp4Writer>, MediaError>
    create(const std::filesystem::path& out, const ExportFormat& fmt);
    ~Mp4Writer();

    // Video frames must be written in order; pts is assigned from the frame
    // counter (frame N displays at N/fps). Source is converted and scaled to
    // the output format (stretch; aspect-preserving fit is Phase-3 transform).
    Expected<void, MediaError> writeVideoFrame(const VideoFrame& frame);
    Expected<void, MediaError> writeBlackFrame();

    // Appends interleaved float32 audio at the export rate.
    Expected<void, MediaError> writeAudio(const float* interleaved, int frames);

    // Flushes encoders and writes the trailer. Must be called exactly once.
    Expected<void, MediaError> finish();

    [[nodiscard]] const std::string& videoEncoderName() const;
    [[nodiscard]] std::int64_t videoFramesWritten() const;
    [[nodiscard]] std::int64_t audioSamplesWritten() const;

    struct Impl;

private:
    Mp4Writer() = default;
    std::unique_ptr<Impl> impl_;
};

} // namespace velocity::media
