#include "velocity/engine/export.h"

#include "velocity/engine/audio_mix.h"
#include "velocity/engine/compile.h"

#include <velocity/media/mp4_writer.h>
#include <velocity/media/sequential_reader.h>

#include <algorithm>
#include <map>
#include <vector>

namespace velocity::engine {

std::int64_t expectedFrameCount(Tick duration, Rational fps) {
    if (duration <= 0)
        return 0;
    return frameIndexFromTicks(duration - 1, fps) + 1;
}

Expected<ExportResult, std::string>
exportSequence(const SnapshotPtr& seq, const std::filesystem::path& outFile,
               const ExportSettings& settings, const ExportProgressFn& progress) {
    const Tick duration = seq->duration();
    if (duration <= 0)
        return makeUnexpected(std::string("timeline is empty"));

    media::ExportFormat fmt;
    fmt.width = settings.width > 0 ? settings.width : seq->width;
    fmt.height = settings.height > 0 ? settings.height : seq->height;
    fmt.fps = settings.fps.num > 0 ? settings.fps : seq->frameRate;
    fmt.videoBitrate = settings.videoBitrate;
    fmt.audioBitrate = settings.audioBitrate;
    fmt.preferHardwareEncoder = settings.preferHardwareEncoder;
    // Encoders require even dimensions for 4:2:0.
    fmt.width &= ~1;
    fmt.height &= ~1;

    auto writer = media::Mp4Writer::create(outFile, fmt);
    if (!writer)
        return makeUnexpected("cannot create output: " + writer.error().message);

    const std::int64_t totalFrames = expectedFrameCount(duration, fmt.fps);

    std::map<std::filesystem::path, std::unique_ptr<media::SequentialFrameReader>> videoReaders;
    AudioMixer audioMixer;
    std::vector<float> mixBuf;
    Tick audioPos = 0; // samples written (ticks == samples at 48 kHz)

    auto writeAudioUpTo = [&](Tick upTo) -> Expected<void, std::string> {
        while (audioPos < upTo) {
            const Tick block = std::min<Tick>(4096, upTo - audioPos);
            mixBuf.resize(static_cast<size_t>(block) * 2);
            audioMixer.mix(*seq, audioPos, static_cast<int>(block), mixBuf.data());
            if (auto w = (*writer)->writeAudio(mixBuf.data(), static_cast<int>(block)); !w)
                return makeUnexpected("audio write failed: " + w.error().message);
            audioPos += block;
        }
        return {};
    };

    for (std::int64_t f = 0; f < totalFrames; ++f) {
        const Tick tick = ticksFromFrameIndex(f, fmt.fps);

        bool wrote = false;
        if (auto sample = resolveVideoAt(*seq, tick)) {
            auto it = videoReaders.find(sample->asset);
            if (it == videoReaders.end()) {
                auto dec = media::VideoDecoder::open(sample->asset);
                it = videoReaders
                         .emplace(sample->asset,
                                  dec ? std::make_unique<media::SequentialFrameReader>(
                                            std::move(dec.value()))
                                      : nullptr)
                         .first;
            }
            if (it->second) {
                if (auto frame = it->second->at(sample->srcPts)) {
                    if (auto w = (*writer)->writeVideoFrame(*frame); !w)
                        return makeUnexpected("video write failed: " + w.error().message);
                    wrote = true;
                }
            }
        }
        if (!wrote) {
            if (auto w = (*writer)->writeBlackFrame(); !w)
                return makeUnexpected("video write failed: " + w.error().message);
        }

        // Keep audio interleaved with video: write up to the next frame edge.
        const Tick nextEdge = f + 1 < totalFrames ? ticksFromFrameIndex(f + 1, fmt.fps)
                                                  : duration;
        if (auto a = writeAudioUpTo(std::min(nextEdge, duration)); !a)
            return makeUnexpected(a.error());

        if (progress && (f % 5 == 0 || f + 1 == totalFrames)) {
            if (!progress(static_cast<double>(f + 1) / static_cast<double>(totalFrames)))
                return makeUnexpected(std::string("export cancelled"));
        }
    }

    if (auto fin = (*writer)->finish(); !fin)
        return makeUnexpected("finalize failed: " + fin.error().message);

    ExportResult result;
    result.videoFrames = (*writer)->videoFramesWritten();
    result.audioSamples = (*writer)->audioSamplesWritten();
    result.videoEncoder = (*writer)->videoEncoderName();
    return result;
}

} // namespace velocity::engine
