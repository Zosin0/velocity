#include "velocity/gpu/device.h"

#include <windows.h>

namespace velocity::gpu {

namespace {
GpuError err(const char* what, HRESULT hr) {
    return GpuError{std::string(what) + " (hr=" + std::to_string(static_cast<long>(hr)) + ")",
                    static_cast<long>(hr)};
}
} // namespace

Expected<std::unique_ptr<Device>, GpuError> Device::create(bool allowWarp) {
    using Ret = Expected<std::unique_ptr<Device>, GpuError>;
    auto dev = std::unique_ptr<Device>(new Device());

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dev->factory_));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateDXGIFactory2", hr))};

    // Prefer the highest-performance hardware adapter.
    for (UINT i = 0; dev->factory_->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                         IID_PPV_ARGS(&dev->adapter_)) == S_OK;
         ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        dev->adapter_->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            dev->adapter_.Reset();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(dev->adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&dev->device_)))) {
            dev->adapterName_ = desc.Description;
            break;
        }
        dev->adapter_.Reset();
    }

    if (!dev->device_ && allowWarp) {
        if (SUCCEEDED(dev->factory_->EnumWarpAdapter(IID_PPV_ARGS(&dev->adapter_))) &&
            SUCCEEDED(D3D12CreateDevice(dev->adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&dev->device_)))) {
            dev->warp_ = true;
            dev->adapterName_ = L"WARP";
        }
    }
    if (!dev->device_)
        return Ret{makeUnexpected(GpuError{"no D3D12-capable adapter", 0})};

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = dev->device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev->queue_));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateCommandQueue", hr))};

    hr = dev->device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&dev->allocator_));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateCommandAllocator", hr))};

    hr = dev->device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         dev->allocator_.Get(), nullptr,
                                         IID_PPV_ARGS(&dev->list_));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateCommandList", hr))};
    dev->list_->Close();

    hr = dev->device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dev->fence_));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateFence", hr))};
    dev->fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dev->fenceEvent_)
        return Ret{makeUnexpected(GpuError{"CreateEvent failed", 0})};

    return Ret{std::move(dev)};
}

Device::~Device() {
    if (device_)
        (void)waitIdle();
    if (fenceEvent_)
        CloseHandle(fenceEvent_);
}

Expected<ID3D12GraphicsCommandList*, GpuError> Device::beginCommands() {
    using Ret = Expected<ID3D12GraphicsCommandList*, GpuError>;
    HRESULT hr = allocator_->Reset();
    if (FAILED(hr))
        return Ret{makeUnexpected(err("allocator Reset", hr))};
    hr = list_->Reset(allocator_.Get(), nullptr);
    if (FAILED(hr))
        return Ret{makeUnexpected(err("list Reset", hr))};
    return Ret{list_.Get()};
}

Expected<void, GpuError> Device::finishCommandsSync() {
    HRESULT hr = list_->Close();
    if (FAILED(hr))
        return makeUnexpected(err("list Close", hr));
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    return waitIdle();
}

Expected<void, GpuError> Device::waitIdle() {
    const std::uint64_t v = ++fenceValue_;
    HRESULT hr = queue_->Signal(fence_.Get(), v);
    if (FAILED(hr))
        return makeUnexpected(err("queue Signal", hr));
    if (fence_->GetCompletedValue() < v) {
        hr = fence_->SetEventOnCompletion(v, fenceEvent_);
        if (FAILED(hr))
            return makeUnexpected(err("SetEventOnCompletion", hr));
        WaitForSingleObject(fenceEvent_, 10'000);
    }
    return {};
}

Expected<RenderTarget, GpuError> Device::createRenderTarget(std::uint32_t w, std::uint32_t h,
                                                            DXGI_FORMAT fmt,
                                                            const float clearColor[4]) {
    using Ret = Expected<RenderTarget, GpuError>;
    RenderTarget rt;
    rt.width = w;
    rt.height = h;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = fmt;
    for (int i = 0; i < 4; ++i)
        clear.Color[i] = clearColor[i];

    HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                                                  IID_PPV_ARGS(&rt.texture));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateCommittedResource(RT)", hr))};

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    hr = device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rt.rtvHeap));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateDescriptorHeap", hr))};
    rt.rtv = rt.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(rt.texture.Get(), nullptr, rt.rtv);
    return Ret{std::move(rt)};
}

Expected<std::vector<std::uint8_t>, GpuError> Device::readbackRgba8(const RenderTarget& rt) {
    using Ret = Expected<std::vector<std::uint8_t>, GpuError>;
    const std::uint32_t rowPitch =
        (rt.width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const std::uint64_t bufSize = static_cast<std::uint64_t>(rowPitch) * rt.height;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> staging;
    HRESULT hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&staging));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("CreateCommittedResource(readback)", hr))};

    auto exec = executeSync([&](ID3D12GraphicsCommandList* cl) {
        D3D12_RESOURCE_BARRIER toCopy{};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = rt.texture.Get();
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cl->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = rt.texture.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = staging.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = rt.width;
        dst.PlacedFootprint.Footprint.Height = rt.height;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER back{};
        back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        back.Transition.pResource = rt.texture.Get();
        back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &back);
    });
    if (!exec)
        return Ret{makeUnexpected(exec.error())};

    std::uint8_t* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bufSize)};
    hr = staging->Map(0, &range, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr))
        return Ret{makeUnexpected(err("Map readback", hr))};

    std::vector<std::uint8_t> out(static_cast<size_t>(rt.width) * rt.height * 4);
    for (std::uint32_t y = 0; y < rt.height; ++y)
        memcpy(out.data() + static_cast<size_t>(y) * rt.width * 4, mapped + y * rowPitch,
               static_cast<size_t>(rt.width) * 4);
    staging->Unmap(0, nullptr);
    return Ret{std::move(out)};
}

} // namespace velocity::gpu
