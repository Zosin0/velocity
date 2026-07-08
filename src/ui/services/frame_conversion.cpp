#include "frame_conversion.h"

#include <algorithm>
#include <cstring>

namespace velocity::ui {

namespace {

inline QRgb yuvToRgb(int yVal, int uVal, int vVal) {
    const int r = std::clamp(static_cast<int>(yVal + 1.402 * vVal), 0, 255);
    const int g =
        std::clamp(static_cast<int>(yVal - 0.344136 * uVal - 0.714136 * vVal), 0, 255);
    const int b = std::clamp(static_cast<int>(yVal + 1.772 * uVal), 0, 255);
    return qRgb(r, g, b);
}

} // namespace

QImage toQImage(const velocity::media::VideoFrame& frame) {
    auto cpuFrameRes = frame.transferToCpu();
    if (!cpuFrameRes)
        return {};
    const auto& cpuFrame = cpuFrameRes.value();

    const int w = cpuFrame.width();
    const int h = cpuFrame.height();
    const int fmt = cpuFrame.pixelFormatInt();

    if (fmt == 0 || fmt == 12) { // AV_PIX_FMT_YUV420P / YUVJ420P (full-range JPEG)
        QImage img(w, h, QImage::Format_RGB32);
        const std::uint8_t* yData = cpuFrame.data(0);
        const std::uint8_t* uData = cpuFrame.data(1);
        const std::uint8_t* vData = cpuFrame.data(2);
        const int yStride = cpuFrame.stride(0);
        const int uStride = cpuFrame.stride(1);
        const int vStride = cpuFrame.stride(2);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                line[x] = yuvToRgb(yData[y * yStride + x],
                                   uData[(y / 2) * uStride + (x / 2)] - 128,
                                   vData[(y / 2) * vStride + (x / 2)] - 128);
            }
        }
        return img;
    }

    if (fmt == 23 || fmt == 24) { // AV_PIX_FMT_NV12 / NV21
        QImage img(w, h, QImage::Format_RGB32);
        const std::uint8_t* yData = cpuFrame.data(0);
        const std::uint8_t* uvData = cpuFrame.data(1);
        const int yStride = cpuFrame.stride(0);
        const int uvStride = cpuFrame.stride(1);
        const bool isNV12 = (fmt == 23);
        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int uvIdx = (y / 2) * uvStride + (x / 2) * 2;
                line[x] = yuvToRgb(yData[y * yStride + x],
                                   uvData[uvIdx + (isNV12 ? 0 : 1)] - 128,
                                   uvData[uvIdx + (isNV12 ? 1 : 0)] - 128);
            }
        }
        return img;
    }

    if (fmt == 26) { // AV_PIX_FMT_RGBA — image imports (PNG/WebP)
        const std::uint8_t* src = cpuFrame.data(0);
        const int stride = cpuFrame.stride(0);
        QImage rgba(w, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgba.scanLine(y), src + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(w) * 4);
        return rgba;
    }

    if (fmt == 2) { // AV_PIX_FMT_RGB24 — JPEG and friends
        const std::uint8_t* src = cpuFrame.data(0);
        const int stride = cpuFrame.stride(0);
        QImage rgb(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y)
            std::memcpy(rgb.scanLine(y), src + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(w) * 3);
        return rgb.convertToFormat(QImage::Format_RGB32);
    }

    return {};
}

} // namespace velocity::ui
