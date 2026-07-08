#pragma once
// Decoded frame → straight-alpha RGBA pixels for CPU compositing (the export
// parity path, docs/06 semantics on the CPU until the D3D12 render graph).
// swscale handles every source pixel format the decoders can produce.

#include <velocity/foundation/expected.h>
#include <velocity/media/error.h>
#include <velocity/media/video_decoder.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace velocity::media {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // tightly packed, stride == width * 4

    [[nodiscard]] int stride() const { return width * 4; }
    [[nodiscard]] bool empty() const { return pixels.empty(); }
};

// Converts frames to RGBA with an internally cached swscale context — reuse
// one converter per decode stream. Not thread-safe.
class RgbaConverter {
public:
    RgbaConverter();
    ~RgbaConverter();
    RgbaConverter(const RgbaConverter&) = delete;
    RgbaConverter& operator=(const RgbaConverter&) = delete;
    RgbaConverter(RgbaConverter&&) noexcept;
    RgbaConverter& operator=(RgbaConverter&&) noexcept;

    Expected<RgbaImage, MediaError> convert(const VideoFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace velocity::media
