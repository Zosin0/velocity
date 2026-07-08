#include "velocity/media/frame_rgba.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace velocity::media {

struct RgbaConverter::Impl {
    SwsContext* sws = nullptr;
    ~Impl() {
        if (sws)
            sws_freeContext(sws);
    }
};

RgbaConverter::RgbaConverter() : impl_(std::make_unique<Impl>()) {}
RgbaConverter::~RgbaConverter() = default;
RgbaConverter::RgbaConverter(RgbaConverter&&) noexcept = default;
RgbaConverter& RgbaConverter::operator=(RgbaConverter&&) noexcept = default;

Expected<RgbaImage, MediaError> RgbaConverter::convert(const VideoFrame& frame) {
    VideoFrame src = frame;
    if (src.isHardware()) {
        auto cpu = src.transferToCpu();
        if (!cpu)
            return makeUnexpected(cpu.error());
        src = *cpu;
    }

    RgbaImage out;
    out.width = src.width();
    out.height = src.height();
    if (out.width <= 0 || out.height <= 0)
        return makeUnexpected(MediaError{MediaErrorKind::decode, "empty frame"});
    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4);

    impl_->sws = sws_getCachedContext(impl_->sws, src.width(), src.height(),
                                      static_cast<AVPixelFormat>(src.pixelFormatInt()),
                                      out.width, out.height, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                                      nullptr, nullptr, nullptr);
    if (!impl_->sws)
        return makeUnexpected(MediaError{MediaErrorKind::unsupported, "sws context"});

    const std::uint8_t* srcData[4] = {src.data(0), src.data(1), src.data(2), src.data(3)};
    const int srcStride[4] = {src.stride(0), src.stride(1), src.stride(2), src.stride(3)};
    std::uint8_t* dstData[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
    const int dstStride[4] = {out.stride(), 0, 0, 0};
    sws_scale(impl_->sws, srcData, srcStride, 0, src.height(), dstData, dstStride);

    return out;
}

} // namespace velocity::media
