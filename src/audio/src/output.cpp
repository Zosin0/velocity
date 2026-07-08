#include "velocity/audio/output.h"

#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace velocity::audio {

using Microsoft::WRL::ComPtr;

namespace {
struct ComInit {
    ComInit() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComInit() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    HRESULT hr;
};

std::string hrText(const char* what, HRESULT hr) {
    return std::string(what) + " (hr=" + std::to_string(static_cast<long>(hr)) + ")";
}
} // namespace

struct AudioOutput::Impl {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    ComPtr<IAudioClock> audioClock;
    HANDLE event = nullptr;
    std::uint32_t bufferFrames = 0;
    std::uint32_t rate = 0;
    std::uint32_t nChannels = 0;
    std::uint64_t clockFreq = 1;

    FillCallback fill;
    std::thread renderThread;
    std::atomic<bool> running{false};
    std::atomic<std::uint32_t> underrunCount{0};

    ~Impl() {
        if (event)
            CloseHandle(event);
    }

    void renderLoop() {
        ComInit com; // COM per thread
        // Ask the OS scheduler for pro-audio scheduling on this thread.
        DWORD taskIndex = 0;
        HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

        while (running.load(std::memory_order_acquire)) {
            if (WaitForSingleObject(event, 200) != WAIT_OBJECT_0) {
                if (running.load(std::memory_order_acquire))
                    underrunCount.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding)))
                continue;
            const UINT32 frames = bufferFrames - padding;
            if (frames == 0)
                continue;
            BYTE* buf = nullptr;
            if (FAILED(render->GetBuffer(frames, &buf)))
                continue;
            fill(reinterpret_cast<float*>(buf), frames);
            render->ReleaseBuffer(frames, 0);
        }

        if (avrt)
            AvRevertMmThreadCharacteristics(avrt);
    }
};

Expected<std::unique_ptr<AudioOutput>, std::string> AudioOutput::create() {
    using Ret = Expected<std::unique_ptr<AudioOutput>, std::string>;
    ComInit com;

    auto out = std::unique_ptr<AudioOutput>(new AudioOutput());
    out->impl_ = std::make_unique<Impl>();
    Impl& im = *out->impl_;

    ComPtr<IMMDeviceEnumerator> devEnum;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&devEnum));
    if (FAILED(hr))
        return Ret{makeUnexpected(hrText("MMDeviceEnumerator", hr))};

    hr = devEnum->GetDefaultAudioEndpoint(eRender, eConsole, &im.device);
    if (FAILED(hr))
        return Ret{makeUnexpected(hrText("no default audio endpoint", hr))};

    hr = im.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void**>(im.client.GetAddressOf()));
    if (FAILED(hr))
        return Ret{makeUnexpected(hrText("IAudioClient activate", hr))};

    WAVEFORMATEX* mix = nullptr;
    hr = im.client->GetMixFormat(&mix);
    if (FAILED(hr))
        return Ret{makeUnexpected(hrText("GetMixFormat", hr))};

    const bool isFloat =
        mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->SubFormat ==
             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if (!isFloat || mix->wBitsPerSample != 32) {
        CoTaskMemFree(mix);
        return Ret{makeUnexpected(std::string("mix format is not float32"))};
    }
    im.rate = mix->nSamplesPerSec;
    im.nChannels = mix->nChannels;

    constexpr REFERENCE_TIME kBuffer = 50 * 10'000; // 50 ms in 100ns units
    hr = im.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                               kBuffer, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr))
        return Ret{makeUnexpected(hrText("IAudioClient Initialize", hr))};

    im.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!im.event || FAILED(hr = im.client->SetEventHandle(im.event)))
        return Ret{makeUnexpected(hrText("SetEventHandle", hr))};

    if (FAILED(hr = im.client->GetBufferSize(&im.bufferFrames)))
        return Ret{makeUnexpected(hrText("GetBufferSize", hr))};
    if (FAILED(hr = im.client->GetService(IID_PPV_ARGS(&im.render))))
        return Ret{makeUnexpected(hrText("IAudioRenderClient", hr))};
    if (FAILED(hr = im.client->GetService(IID_PPV_ARGS(&im.audioClock))))
        return Ret{makeUnexpected(hrText("IAudioClock", hr))};
    im.audioClock->GetFrequency(&im.clockFreq);
    if (im.clockFreq == 0)
        im.clockFreq = 1;

    return Ret{std::move(out)};
}

AudioOutput::~AudioOutput() { stop(); }

Expected<void, std::string> AudioOutput::start(FillCallback fillCb) {
    Impl& im = *impl_;
    im.fill = std::move(fillCb);

    // Pre-fill the free portion of the buffer so playback starts without an
    // underrun. After a stop()/Reset() this is the whole buffer, but query
    // the padding anyway — assuming an empty buffer made restarts fail with
    // AUDCLNT_E_BUFFER_TOO_LARGE (the play-after-edit freeze).
    UINT32 padding = 0;
    HRESULT hr = im.client->GetCurrentPadding(&padding);
    if (FAILED(hr))
        return makeUnexpected(hrText("prefill GetCurrentPadding", hr));
    const UINT32 prefill = im.bufferFrames - padding;
    if (prefill > 0) {
        BYTE* buf = nullptr;
        if (FAILED(hr = im.render->GetBuffer(prefill, &buf)))
            return makeUnexpected(hrText("prefill GetBuffer", hr));
        im.fill(reinterpret_cast<float*>(buf), prefill);
        im.render->ReleaseBuffer(prefill, 0);
    }

    im.running.store(true, std::memory_order_release);
    im.renderThread = std::thread([this] { impl_->renderLoop(); });

    if (FAILED(hr = im.client->Start())) {
        stop();
        return makeUnexpected(hrText("IAudioClient Start", hr));
    }
    return {};
}

void AudioOutput::stop() {
    Impl& im = *impl_;
    if (!im.running.exchange(false))
        return;
    if (im.renderThread.joinable())
        im.renderThread.join();
    if (im.client) {
        im.client->Stop();
        // Flush queued frames and rewind the stream/clock so the next
        // start() begins from a clean, fully writable buffer.
        im.client->Reset();
    }
}

std::uint32_t AudioOutput::sampleRate() const { return impl_->rate; }
std::uint32_t AudioOutput::channels() const { return impl_->nChannels; }
std::uint32_t AudioOutput::underruns() const {
    return impl_->underrunCount.load(std::memory_order_relaxed);
}

Expected<AudioOutput::ClockSample, std::string> AudioOutput::clock() const {
    UINT64 pos = 0, qpcTime = 0;
    const HRESULT hr = impl_->audioClock->GetPosition(&pos, &qpcTime);
    if (FAILED(hr))
        return makeUnexpected(hrText("IAudioClock GetPosition", hr));
    ClockSample s;
    s.positionSeconds = static_cast<double>(pos) / static_cast<double>(impl_->clockFreq);
    // GetPosition reports QPC time in 100ns units; convert to raw QPC ticks
    // domain? Keep it in 100ns units consistently.
    s.qpc = static_cast<std::int64_t>(qpcTime);
    return s;
}

} // namespace velocity::audio
