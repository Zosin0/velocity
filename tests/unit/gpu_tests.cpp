// Spike B acceptance: a D3D12 device comes up (hardware, or WARP on CI),
// GPU-rendered output is bit-verifiable via readback, and a flip-model
// swapchain on a real HWND presents without error.

#include <velocity/gpu/device.h>
#include <velocity/gpu/swapchain.h>
#include <velocity/gpu/window.h>

#include <gtest/gtest.h>

using namespace velocity;
using namespace velocity::gpu;

TEST(Gpu, DeviceCreates) {
    auto dev = Device::create();
    ASSERT_TRUE(dev.hasValue()) << dev.error().message;
    EXPECT_NE((*dev)->d3d(), nullptr);
    EXPECT_NE((*dev)->queue(), nullptr);
}

TEST(Gpu, ClearRenderTargetAndReadBack) {
    auto dev = Device::create();
    ASSERT_TRUE(dev.hasValue()) << dev.error().message;

    // Component values chosen to be exactly representable in UNORM8.
    const float color[4] = {1.0f, 0.0f, 128.0f / 255.0f, 1.0f};
    auto rt = (*dev)->createRenderTarget(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, color);
    ASSERT_TRUE(rt.hasValue()) << rt.error().message;

    auto cleared = (*dev)->executeSync([&](ID3D12GraphicsCommandList* cl) {
        cl->ClearRenderTargetView(rt->rtv, color, 0, nullptr);
    });
    ASSERT_TRUE(cleared.hasValue()) << cleared.error().message;

    auto pixels = (*dev)->readbackRgba8(*rt);
    ASSERT_TRUE(pixels.hasValue()) << pixels.error().message;
    ASSERT_EQ(pixels->size(), 64u * 64u * 4u);
    for (size_t px = 0; px < pixels->size(); px += 4) {
        ASSERT_EQ((*pixels)[px + 0], 255) << "at byte " << px;
        ASSERT_EQ((*pixels)[px + 1], 0);
        ASSERT_EQ((*pixels)[px + 2], 128);
        ASSERT_EQ((*pixels)[px + 3], 255);
    }
}

TEST(Gpu, SwapchainPresentsOnRealWindow) {
    auto win = Window::create(L"velocity gpu test", 320, 240, /*visible=*/true);
    if (!win)
        GTEST_SKIP() << "no interactive window session: " << win.error();

    auto dev = Device::create();
    ASSERT_TRUE(dev.hasValue()) << dev.error().message;

    auto sc = Swapchain::create(**dev, (*win)->hwnd(), 320, 240);
    if (!sc)
        GTEST_SKIP() << "swapchain unavailable in this session: " << sc.error().message;

    const float a[4] = {0.1f, 0.2f, 0.3f, 1.0f};
    const float b[4] = {0.9f, 0.5f, 0.1f, 1.0f};
    for (int frame = 0; frame < 6; ++frame) {
        auto r = (*sc)->clearAndPresent(**dev, frame % 2 ? a : b);
        ASSERT_TRUE(r.hasValue()) << "frame " << frame << ": " << r.error().message;
        (*win)->pumpMessages();
    }
}
