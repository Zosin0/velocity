#include "velocity/engine/export.h"

#include "velocity/engine/compile.h"

#include <velocity/media/audio_decoder.h>
#include <velocity/media/mp4_writer.h>
#include <velocity/media/video_decoder.h>

#include <algorithm>
#include <map>
#include <vector>

namespace velocity::engine {

namespace {

// Sequential-locality reader: during export the requested source pts advances
// monotonically within a clip, so rolling forward with readNext beats a
// keyframe seek per frame. Falls back to exact seek for jumps.
struct SequentialVideoReader {
    std::unique_ptr<media::VideoDecoder> decoder;
    std::int64_t lastPts = -1;

    Expected<media::VideoFrame, media::MediaError> at(std::int64_t pts) {
        const Rational tb = decoder->stream().timebase;
        // "Near" = within 2 seconds ahead of the last delivered frame.
        const bool near = lastPts >= 0 && pts >= lastPts &&
                          detail::cmpMul128(pts - lastPts, tb.num, 2, tb.den) < 0;
        if (near) {
            for (int guard = 0; guard < 600; ++guard) {
                auto f = decoder->readNext();
                if (!f) {
                    if (f.error().isEndOfStream())
                        break; // clamp: fall through to seek path
                    return f;
                }
                if (f->pts() + std::max<std::int64_t>(f->duration(), 0) > pts ||
                    f->pts() >= pts) {
                    lastPts = f->pts();
                    return f;
                }
            }
        }
        auto f = decoder->readFrameAt(pts);
        if (f)
            lastPts = f->pts();
        return f;
    }
};

} // namespace

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

    std::map<std::filesystem::path, SequentialVideoReader> videoReaders;
    std::map<std::filesystem::path, std::unique_ptr<media::AudioDecoder>> audioDecoders;
    std::vector<float> mixBuf;
    std::vector<float> segBuf;
    Tick audioPos = 0; // samples written so far (ticks == samples at 48 kHz)

    // Mixes and writes timeline audio for [audioPos, upTo).
    auto writeAudioUpTo = [&](Tick upTo) -> Expected<void, std::string> {
        while (audioPos < upTo) {
            const Tick block = std::min<Tick>(4096, upTo - audioPos);
            mixBuf.assign(static_cast<size_t>(block) * 2, 0.0f);

            for (const auto& segment : audioSegmentsInRange(*seq, audioPos, block)) {
                auto it = audioDecoders.find(segment.asset);
                if (it == audioDecoders.end()) {
                    auto dec = media::AudioDecoder::open(segment.asset);
                    if (!dec)
                        continue; // asset without decodable audio: silence
                    it = audioDecoders.emplace(segment.asset, std::move(dec.value())).first;
                }
                // Source position of this block piece, in output samples.
                const std::int64_t srcStart =
                    ticksFromPts(segment.srcStartPts, segment.srcTimebase) +
                    (audioPos > segment.start ? audioPos - segment.start : 0);
                const Tick pieceStart = std::max(segment.start, audioPos);
                const Tick pieceLen = std::min(segment.start + segment.len, audioPos + block) -
                                      pieceStart;
                if (pieceLen <= 0)
                    continue;

                segBuf.assign(static_cast<size_t>(pieceLen) * 2, 0.0f);
                if (auto r = it->second->readAt(srcStart, segBuf.data(),
                                                static_cast<int>(pieceLen));
                    !r)
                    continue;

                float* dst = mixBuf.data() + static_cast<size_t>(pieceStart - audioPos) * 2;
                for (size_t i = 0; i < segBuf.size(); ++i)
                    dst[i] += segBuf[i];
            }

            for (float& s : mixBuf)
                s = std::clamp(s, -1.0f, 1.0f);
            if (auto w = (*writer)->writeAudio(mixBuf.data(), static_cast<int>(block)); !w)
                return makeUnexpected("audio write failed: " + w.error().message);
            audioPos += block;
        }
        return {};
    };

    for (std::int64_t f = 0; f < totalFrames; ++f) {
        const Tick tick = ticksFromFrameIndex(f, fmt.fps);

        if (auto sample = resolveVideoAt(*seq, tick)) {
            auto it = videoReaders.find(sample->asset);
            if (it == videoReaders.end()) {
                auto dec = media::VideoDecoder::open(sample->asset);
                if (dec)
                    it = videoReaders
                             .emplace(sample->asset,
                                      SequentialVideoReader{std::move(dec.value()), -1})
                             .first;
            }
            bool wrote = false;
            if (it != videoReaders.end()) {
                if (auto frame = it->second.at(sample->srcPts)) {
                    if (auto w = (*writer)->writeVideoFrame(*frame); !w)
                        return makeUnexpected("video write failed: " + w.error().message);
                    wrote = true;
                }
            }
            if (!wrote) {
                if (auto w = (*writer)->writeBlackFrame(); !w)
                    return makeUnexpected("video write failed: " + w.error().message);
            }
        } else {
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
