#pragma once
// Spike-A decoder (docs/04 §1-2, reduced): one decoder per (file, stream),
// sequential fast path + frame-accurate random access, optional D3D11VA
// hardware decode. Frame-index sidecar files come later; random access here
// uses container seek + roll-forward, which the tests verify is exact for
// the supported sources.

#include <velocity/foundation/expected.h>
#include <velocity/foundation/time.h>
#include <velocity/media/error.h>
#include <velocity/media/probe.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace velocity::media {

// Refcounted immutable decoded frame. CPU frames expose plane data directly;
// hardware frames (isHardware()) expose the GPU surface to the render
// integration (Phase 2) and can be brought to CPU with transferToCpu().
class VideoFrame {
public:
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] std::int64_t pts() const;      // stream timebase units
    [[nodiscard]] std::int64_t duration() const; // stream timebase units, 0 if unknown
    [[nodiscard]] Rational timebase() const;
    [[nodiscard]] bool isHardware() const;

    // Raw AVPixelFormat value. For use by other src/media components only;
    // code outside media/ must not interpret it.
    [[nodiscard]] int pixelFormatInt() const;

    // CPU frames only. Planes follow the pixel format (yuv420p/nv12).
    [[nodiscard]] const std::uint8_t* data(int plane) const;
    [[nodiscard]] int stride(int plane) const;

    [[nodiscard]] Expected<VideoFrame, MediaError> transferToCpu() const;

    struct Impl;
    explicit VideoFrame(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

private:
    std::shared_ptr<Impl> impl_;
};

struct DecodeOptions {
    bool preferHardware = false; // D3D11VA; silently falls back to software
};

class VideoDecoder {
public:
    static Expected<std::unique_ptr<VideoDecoder>, MediaError>
    open(const std::filesystem::path& file, const DecodeOptions& opts = {});

    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Next frame in presentation order; kind == endOfStream at EOF.
    Expected<VideoFrame, MediaError> readNext();

    // The frame whose display interval contains targetPts (stream timebase).
    // Seeks backward to a keyframe and rolls forward; exact by construction.
    Expected<VideoFrame, MediaError> readFrameAt(std::int64_t targetPts);

    [[nodiscard]] const VideoStreamInfo& stream() const;
    [[nodiscard]] bool usingHardware() const;

    struct Impl;

private:
    explicit VideoDecoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace velocity::media
