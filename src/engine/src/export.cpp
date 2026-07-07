#include "velocity/engine/export.h"

#include "velocity/engine/audio_mix.h"
#include "velocity/engine/compile.h"
#include "velocity/engine/compositor.h"

#include <velocity/media/frame_rgba.h>
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

namespace {

// Per-asset decode state: sequential reader + RGBA conversion with a
// one-frame cache (image clips and paused sections hit the same pts for
// many output frames — convert once).
struct AssetReader {
    std::unique_ptr<media::SequentialFrameReader> reader;
    media::RgbaConverter converter;
    std::int64_t rgbaPts = -1;
    media::RgbaImage rgba;

    const media::RgbaImage* rgbaAt(std::int64_t pts) {
        if (!reader)
            return nullptr;
        auto frame = reader->at(pts);
        if (!frame)
            return nullptr;
        if (frame->pts() == rgbaPts && !rgba.empty())
            return &rgba;
        auto converted = converter.convert(*frame);
        if (!converted)
            return nullptr;
        rgba = std::move(converted.value());
        rgbaPts = frame->pts();
        return &rgba;
    }
};

} // namespace

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

    std::map<std::filesystem::path, AssetReader> readers;
    AudioMixer audioMixer;
    std::vector<float> mixBuf;
    Tick audioPos = 0; // samples written (ticks == samples at 48 kHz)

    auto writeAudioUpTo = [&](Tick upTo) -> Expected<void, std::string> {
        while (audioPos < upTo) {
            const Tick block = std::min<Tick>(4096, upTo - audioPos);
            mixBuf.resize(static_cast<size_t>(block) * 2);
            audioMixer.mix(*seq, audioPos, static_cast<int>(block), mixBuf.data(),
                           settings.masterGain);
            if (auto w = (*writer)->writeAudio(mixBuf.data(), static_cast<int>(block)); !w)
                return makeUnexpected("audio write failed: " + w.error().message);
            audioPos += block;
        }
        return {};
    };

    auto readerFor = [&](const std::filesystem::path& asset) -> AssetReader& {
        auto it = readers.find(asset);
        if (it == readers.end()) {
            AssetReader ar;
            if (auto dec = media::VideoDecoder::open(asset))
                ar.reader =
                    std::make_unique<media::SequentialFrameReader>(std::move(dec.value()));
            it = readers.emplace(asset, std::move(ar)).first;
        }
        return it->second;
    };

    std::vector<CompositorLayer> layers;
    for (std::int64_t f = 0; f < totalFrames; ++f) {
        const Tick tick = ticksFromFrameIndex(f, fmt.fps);

        // Gaps render as black; every visible layer composites with its
        // transform — exactly what the preview shows (docs/10 §1 identity).
        layers.clear();
        for (const auto& sample : resolveVideoLayersAt(*seq, tick)) {
            const media::RgbaImage* rgba = readerFor(sample.asset).rgbaAt(sample.srcPts);
            if (!rgba)
                continue; // missing/offline media: layer drops out (slate later)
            CompositorLayer layer;
            layer.rgba = rgba->pixels.data();
            layer.width = rgba->width;
            layer.height = rgba->height;
            layer.strideBytes = rgba->stride();
            layer.transform = sample.transform;
            layers.push_back(layer);
        }

        if (layers.empty()) {
            if (auto w = (*writer)->writeBlackFrame(); !w)
                return makeUnexpected("video write failed: " + w.error().message);
        } else {
            const CompositeCanvas canvas = compositeLayers(fmt.width, fmt.height, layers);
            if (auto w = (*writer)->writeRgbaFrame(canvas.pixels.data(), canvas.width,
                                                   canvas.height, canvas.stride());
                !w)
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
