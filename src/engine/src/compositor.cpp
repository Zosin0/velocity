#include "velocity/engine/compositor.h"

#include <algorithm>
#include <cmath>

namespace velocity::engine {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

// One bilinear tap, premultiplied by texel alpha; coordinates are clamped to
// the image edge (matching QPainter's edge behavior closely enough).
inline void samplePremul(const std::uint8_t* rgba, int w, int h, int stride, double sx,
                         double sy, double out[4]) {
    sx = std::clamp(sx, 0.0, static_cast<double>(w) - 1.0001);
    sy = std::clamp(sy, 0.0, static_cast<double>(h) - 1.0001);
    const int x0 = static_cast<int>(sx);
    const int y0 = static_cast<int>(sy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = sx - x0;
    const double fy = sy - y0;

    const double w00 = (1 - fx) * (1 - fy);
    const double w10 = fx * (1 - fy);
    const double w01 = (1 - fx) * fy;
    const double w11 = fx * fy;

    const std::uint8_t* p00 = rgba + static_cast<size_t>(y0) * stride + x0 * 4;
    const std::uint8_t* p10 = rgba + static_cast<size_t>(y0) * stride + x1 * 4;
    const std::uint8_t* p01 = rgba + static_cast<size_t>(y1) * stride + x0 * 4;
    const std::uint8_t* p11 = rgba + static_cast<size_t>(y1) * stride + x1 * 4;

    const double a00 = p00[3] * w00, a10 = p10[3] * w10;
    const double a01 = p01[3] * w01, a11 = p11[3] * w11;
    for (int c = 0; c < 3; ++c)
        out[c] = (p00[c] * a00 + p10[c] * a10 + p01[c] * a01 + p11[c] * a11) / 255.0;
    out[3] = a00 + a10 + a01 + a11;
}

} // namespace

CompositeCanvas compositeLayers(int canvasWidth, int canvasHeight,
                                const std::vector<CompositorLayer>& layers) {
    CompositeCanvas canvas;
    compositeLayersInto(canvas, canvasWidth, canvasHeight, layers);
    return canvas;
}

void compositeLayersInto(CompositeCanvas& canvas, int canvasWidth, int canvasHeight,
                         const std::vector<CompositorLayer>& layers) {
    canvas.width = std::max(canvasWidth, 1);
    canvas.height = std::max(canvasHeight, 1);
    canvas.pixels.assign(static_cast<size_t>(canvas.width) * canvas.height * 4, 0);
    // Opaque black base.
    for (size_t i = 3; i < canvas.pixels.size(); i += 4)
        canvas.pixels[i] = 255;

    for (const CompositorLayer& layer : layers) {
        if (!layer.rgba || layer.width <= 0 || layer.height <= 0)
            continue;
        const float opacity = std::clamp(layer.transform.opacity, 0.0f, 1.0f);
        if (opacity <= 0.0f || layer.transform.scale <= 0.0f)
            continue;
        const int lstride = layer.strideBytes > 0 ? layer.strideBytes : layer.width * 4;

        // Preview geometry: aspect-fit into the canvas, then transform about
        // the canvas center. k maps source pixels to canvas pixels.
        const double fs0 = std::min(static_cast<double>(canvas.width) / layer.width,
                                    static_cast<double>(canvas.height) / layer.height);
        const double fitW = layer.width * fs0;
        const double fitH = layer.height * fs0;
        const double k = fs0 * layer.transform.scale;
        const double theta = layer.transform.rotation * kPi / 180.0;
        const double cosT = std::cos(theta);
        const double sinT = std::sin(theta);
        const Vec2 t{canvas.width / 2.0 + layer.transform.posX * fitW,
                     canvas.height / 2.0 + layer.transform.posY * fitH};

        // Destination bounding box of the transformed layer rect.
        const double hw = fitW / 2.0 * layer.transform.scale;
        const double hh = fitH / 2.0 * layer.transform.scale;
        double minX = t.x, maxX = t.x, minY = t.y, maxY = t.y;
        for (const auto& corner :
             {Vec2{-hw, -hh}, Vec2{hw, -hh}, Vec2{-hw, hh}, Vec2{hw, hh}}) {
            const double dx = t.x + corner.x * cosT - corner.y * sinT;
            const double dy = t.y + corner.x * sinT + corner.y * cosT;
            minX = std::min(minX, dx);
            maxX = std::max(maxX, dx);
            minY = std::min(minY, dy);
            maxY = std::max(maxY, dy);
        }
        const int x0 = std::max(static_cast<int>(std::floor(minX)), 0);
        const int x1 = std::min(static_cast<int>(std::ceil(maxX)), canvas.width);
        const int y0 = std::max(static_cast<int>(std::floor(minY)), 0);
        const int y1 = std::min(static_cast<int>(std::ceil(maxY)), canvas.height);

        // Inverse mapping is affine: walk it incrementally per row/column.
        // src = srcCenter + R(-θ)·(dst - t) / k
        const double stepX_sx = cosT / k, stepX_sy = -sinT / k; // d(src)/d(dstX)
        const double stepY_sx = sinT / k, stepY_sy = cosT / k;  // d(src)/d(dstY)
        const double cx = layer.width / 2.0;
        const double cy = layer.height / 2.0;

        for (int y = y0; y < y1; ++y) {
            const double ry = y + 0.5 - t.y;
            const double rx0 = x0 + 0.5 - t.x;
            double sx = cx + rx0 * stepX_sx + ry * stepY_sx;
            double sy = cy + rx0 * stepX_sy + ry * stepY_sy;
            std::uint8_t* dst = canvas.pixels.data() + static_cast<size_t>(y) * canvas.stride() +
                                static_cast<size_t>(x0) * 4;
            for (int x = x0; x < x1; ++x, sx += stepX_sx, sy += stepX_sy, dst += 4) {
                // Half-texel tolerance keeps the layer's own edge pixels while
                // clipping everything outside the source rect.
                if (sx < -0.5 || sy < -0.5 || sx > layer.width - 0.5 ||
                    sy > layer.height - 0.5)
                    continue;
                double src[4];
                samplePremul(layer.rgba, layer.width, layer.height, lstride, sx - 0.5,
                             sy - 0.5, src);
                const double a = src[3] / 255.0 * opacity; // coverage in [0,1]
                if (a <= 0.0)
                    continue;
                const double keep = 1.0 - a;
                for (int c = 0; c < 3; ++c) {
                    const double blended = src[c] * opacity + dst[c] * keep;
                    dst[c] = static_cast<std::uint8_t>(std::clamp(blended, 0.0, 255.0));
                }
                // Canvas stays opaque.
            }
        }
    }
}

} // namespace velocity::engine
