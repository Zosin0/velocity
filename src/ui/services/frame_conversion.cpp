#include "frame_conversion.h"

#include <velocity/media/frame_rgba.h>

#include <cstring>

namespace velocity::ui {

QImage toQImage(const velocity::media::VideoFrame& frame) {
    // swscale does the pixel math (SIMD, all source formats) — the previous
    // hand-rolled per-pixel loops took tens of milliseconds per 1080p frame
    // on the UI thread and made playback feel frozen. One cached converter
    // per thread: preview converts on the UI thread, thumbnails on workers.
    thread_local media::RgbaConverter converter;
    auto rgba = converter.convert(frame);
    if (!rgba)
        return {};

    QImage img(rgba->width, rgba->height, QImage::Format_RGBA8888);
    // RGBA8888 rows are width*4 bytes, matching RgbaImage's tight packing.
    std::memcpy(img.bits(), rgba->pixels.data(), rgba->pixels.size());
    return img;
}

} // namespace velocity::ui
