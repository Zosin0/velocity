// CPU compositor pixel gates (export parity, docs/10 §1): layer stacking,
// opacity, alpha, position/scale placement, and rotation coverage.

#include <velocity/engine/compositor.h>

#include <gtest/gtest.h>

using namespace velocity;
using namespace velocity::engine;

namespace {

// Solid RGBA image helper.
std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                std::uint8_t a = 255) {
    std::vector<std::uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

const std::uint8_t* at(const CompositeCanvas& c, int x, int y) {
    return c.pixels.data() + static_cast<size_t>(y) * c.stride() + static_cast<size_t>(x) * 4;
}

CompositorLayer layer(const std::vector<std::uint8_t>& px, int w, int h,
                      ClipTransform t = {}) {
    CompositorLayer l;
    l.rgba = px.data();
    l.width = w;
    l.height = h;
    l.transform = t;
    return l;
}

} // namespace

TEST(Compositor, EmptyStackIsOpaqueBlack) {
    auto canvas = compositeLayers(16, 16, {});
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const auto* p = at(canvas, x, y);
            EXPECT_EQ(p[0], 0);
            EXPECT_EQ(p[3], 255);
        }
}

TEST(Compositor, FullFrameLayerCoversCanvas) {
    const auto red = solid(16, 16, 200, 10, 20);
    auto canvas = compositeLayers(16, 16, {layer(red, 16, 16)});
    const auto* p = at(canvas, 8, 8);
    EXPECT_EQ(p[0], 200);
    EXPECT_EQ(p[1], 10);
    EXPECT_EQ(p[2], 20);
    // Corners covered too (identity fit, same aspect).
    EXPECT_EQ(at(canvas, 0, 0)[0], 200);
    EXPECT_EQ(at(canvas, 15, 15)[0], 200);
}

TEST(Compositor, AspectFitLetterboxesMismatchedSources) {
    // 16×16 source into a 32×16 canvas: fit occupies x∈[8,24), the rest is black.
    const auto white = solid(16, 16, 255, 255, 255);
    auto canvas = compositeLayers(32, 16, {layer(white, 16, 16)});
    EXPECT_EQ(at(canvas, 16, 8)[0], 255);
    EXPECT_EQ(at(canvas, 2, 8)[0], 0);   // left letterbox bar
    EXPECT_EQ(at(canvas, 29, 8)[0], 0);  // right letterbox bar
}

TEST(Compositor, TopLayerWinsAndOpacityBlends) {
    const auto red = solid(16, 16, 255, 0, 0);
    const auto blue = solid(16, 16, 0, 0, 255);

    // Opaque top layer replaces the bottom.
    auto canvas = compositeLayers(16, 16, {layer(red, 16, 16), layer(blue, 16, 16)});
    EXPECT_EQ(at(canvas, 8, 8)[2], 255);
    EXPECT_EQ(at(canvas, 8, 8)[0], 0);

    // 50 % opacity mixes both.
    ClipTransform half;
    half.opacity = 0.5f;
    canvas = compositeLayers(16, 16, {layer(red, 16, 16), layer(blue, 16, 16, half)});
    const auto* p = at(canvas, 8, 8);
    EXPECT_NEAR(p[0], 127, 3);
    EXPECT_NEAR(p[2], 127, 3);
}

TEST(Compositor, SourceAlphaBlends) {
    const auto red = solid(16, 16, 255, 0, 0);
    const auto semiWhite = solid(16, 16, 255, 255, 255, 128);
    auto canvas = compositeLayers(16, 16, {layer(red, 16, 16), layer(semiWhite, 16, 16)});
    const auto* p = at(canvas, 8, 8);
    EXPECT_NEAR(p[0], 255, 3);       // red + white
    EXPECT_NEAR(p[1], 128, 4);       // half-covered green channel
}

TEST(Compositor, ScaleAndPositionPlaceTheLayer) {
    const auto white = solid(16, 16, 255, 255, 255);

    // Scale 0.5: white square occupies the middle 8×8.
    ClipTransform t;
    t.scale = 0.5f;
    auto canvas = compositeLayers(16, 16, {layer(white, 16, 16, t)});
    EXPECT_EQ(at(canvas, 8, 8)[0], 255);
    EXPECT_EQ(at(canvas, 2, 2)[0], 0);
    EXPECT_EQ(at(canvas, 13, 13)[0], 0);

    // posX 0.25 with scale 0.5: center moves right by a quarter frame.
    t.posX = 0.25f;
    canvas = compositeLayers(16, 16, {layer(white, 16, 16, t)});
    EXPECT_EQ(at(canvas, 12, 8)[0], 255);
    EXPECT_EQ(at(canvas, 6, 8)[0], 0);
}

TEST(Compositor, RotationKeepsCenterCoverage) {
    const auto white = solid(16, 16, 255, 255, 255);
    ClipTransform t;
    t.rotation = 45.0f;
    auto canvas = compositeLayers(16, 16, {layer(white, 16, 16, t)});
    // The center stays covered; the canvas corners fall outside the rotated
    // square (diagonal half-extent 8√2 ≈ 11.3 > 8 → corners at distance
    // ~11.3 from center along the diagonal are uncovered).
    EXPECT_EQ(at(canvas, 8, 8)[0], 255);
    EXPECT_EQ(at(canvas, 0, 0)[0], 0);
    EXPECT_EQ(at(canvas, 15, 15)[0], 0);
}

TEST(Compositor, HiddenViaZeroOpacityIsSkipped) {
    const auto white = solid(16, 16, 255, 255, 255);
    ClipTransform t;
    t.opacity = 0.0f;
    auto canvas = compositeLayers(16, 16, {layer(white, 16, 16, t)});
    EXPECT_EQ(at(canvas, 8, 8)[0], 0);
}
