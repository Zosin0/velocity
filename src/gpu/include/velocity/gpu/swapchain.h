#pragma once
// Flip-model swapchain on an HWND (docs/01 §3: DXGI flip model, waitable).
// Spike-B scope: clear-and-present; the composited preview path lands on top
// of this in Phase 2.

#include <velocity/gpu/device.h>

namespace velocity::gpu {

class Swapchain {
public:
    static Expected<std::unique_ptr<Swapchain>, GpuError>
    create(Device& device, void* hwnd, std::uint32_t width, std::uint32_t height);

    // Clears the current backbuffer to color and presents (vsync on).
    Expected<void, GpuError> clearAndPresent(Device& device, const float color[4]);

    [[nodiscard]] std::uint32_t backbufferCount() const { return kBufferCount; }

private:
    Swapchain() = default;
    static constexpr std::uint32_t kBufferCount = 2;

    ComPtr<IDXGISwapChain3> swapchain_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12Resource> buffers_[kBufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs_[kBufferCount]{};
};

} // namespace velocity::gpu
