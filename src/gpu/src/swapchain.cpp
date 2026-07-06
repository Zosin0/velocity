#include "velocity/gpu/swapchain.h"

namespace velocity::gpu {

Expected<std::unique_ptr<Swapchain>, GpuError>
Swapchain::create(Device& device, void* hwnd, std::uint32_t width, std::uint32_t height) {
    using Ret = Expected<std::unique_ptr<Swapchain>, GpuError>;
    auto sc = std::unique_ptr<Swapchain>(new Swapchain());

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = device.factory()->CreateSwapChainForHwnd(
        device.queue(), static_cast<HWND>(hwnd), &desc, nullptr, nullptr, &sc1);
    if (FAILED(hr))
        return Ret{makeUnexpected(GpuError{"CreateSwapChainForHwnd failed", hr})};
    if (FAILED(hr = sc1.As(&sc->swapchain_)))
        return Ret{makeUnexpected(GpuError{"IDXGISwapChain3 query failed", hr})};

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kBufferCount;
    if (FAILED(hr = device.d3d()->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sc->rtvHeap_))))
        return Ret{makeUnexpected(GpuError{"rtv heap failed", hr})};

    const UINT stride =
        device.d3d()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = sc->rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kBufferCount; ++i) {
        if (FAILED(hr = sc->swapchain_->GetBuffer(i, IID_PPV_ARGS(&sc->buffers_[i]))))
            return Ret{makeUnexpected(GpuError{"GetBuffer failed", hr})};
        device.d3d()->CreateRenderTargetView(sc->buffers_[i].Get(), nullptr, handle);
        sc->rtvs_[i] = handle;
        handle.ptr += stride;
    }
    return Ret{std::move(sc)};
}

Expected<void, GpuError> Swapchain::clearAndPresent(Device& device, const float color[4]) {
    const UINT idx = swapchain_->GetCurrentBackBufferIndex();

    auto exec = device.executeSync([&](ID3D12GraphicsCommandList* cl) {
        D3D12_RESOURCE_BARRIER toRt{};
        toRt.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRt.Transition.pResource = buffers_[idx].Get();
        toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRt.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &toRt);

        cl->ClearRenderTargetView(rtvs_[idx], color, 0, nullptr);

        D3D12_RESOURCE_BARRIER toPresent = toRt;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cl->ResourceBarrier(1, &toPresent);
    });
    if (!exec)
        return exec;

    const HRESULT hr = swapchain_->Present(1, 0);
    if (FAILED(hr))
        return makeUnexpected(GpuError{"Present failed", hr});
    return {};
}

} // namespace velocity::gpu
