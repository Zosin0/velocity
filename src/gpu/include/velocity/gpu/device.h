#pragma once
// Spike-B seed of the RHI (docs/01 §3, docs/06 §3). Deliberately thin: raw
// D3D12 handles are exposed to sibling engine code for now; the fuller RHI
// facade grows around real usage instead of being designed in a vacuum.

#include <velocity/foundation/expected.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace velocity::gpu {

using Microsoft::WRL::ComPtr;

struct GpuError {
    std::string message;
    long hr = 0;
};

struct RenderTarget {
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class Device {
public:
    // Prefers a hardware adapter; falls back to WARP (CI runners). Never
    // returns a WARP device when allowWarp is false.
    static Expected<std::unique_ptr<Device>, GpuError> create(bool allowWarp = true);
    ~Device();

    [[nodiscard]] ID3D12Device* d3d() const { return device_.Get(); }
    [[nodiscard]] ID3D12CommandQueue* queue() const { return queue_.Get(); }
    [[nodiscard]] IDXGIFactory6* factory() const { return factory_.Get(); }
    [[nodiscard]] bool isWarp() const { return warp_; }
    [[nodiscard]] const std::wstring& adapterName() const { return adapterName_; }

    // Single-shot command recording: reset allocator+list, call record(list),
    // close, execute, block until the GPU finishes. Spike/startup-path only —
    // steady-state rendering uses pipelined frames (Phase 2+).
    template <typename F>
    Expected<void, GpuError> executeSync(F&& record) {
        auto begun = beginCommands();
        if (!begun)
            return makeUnexpected(begun.error());
        record(begun.value());
        return finishCommandsSync();
    }

    Expected<RenderTarget, GpuError> createRenderTarget(std::uint32_t w, std::uint32_t h,
                                                        DXGI_FORMAT fmt,
                                                        const float clearColor[4]);

    // Copies an RGBA8 render target (in COPY_SOURCE state) to CPU memory,
    // tightly packed, 4 bytes per pixel.
    Expected<std::vector<std::uint8_t>, GpuError> readbackRgba8(const RenderTarget& rt);

    Expected<void, GpuError> waitIdle();

private:
    Device() = default;
    Expected<ID3D12GraphicsCommandList*, GpuError> beginCommands();
    Expected<void, GpuError> finishCommandsSync();

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    ComPtr<ID3D12Fence> fence_;
    void* fenceEvent_ = nullptr;
    std::uint64_t fenceValue_ = 0;
    std::wstring adapterName_;
    bool warp_ = false;
};

} // namespace velocity::gpu
