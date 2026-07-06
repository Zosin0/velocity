#pragma once
// Spike-C WASAPI output (docs/07). Shared-mode, event-driven render with a
// fill callback and a sample-accurate device clock — the clock that will be
// the playback master (docs/05 §2). The real engine replaces the callback
// with the lock-free ring feeder; the device/clock plumbing here is final.
//
// Real-time rule (docs/07 §1): the render thread only fills device buffers
// from the callback; the callback must not lock, allocate, or do I/O.

#include <velocity/foundation/expected.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace velocity::audio {

class AudioOutput {
public:
    // interleaved float32 buffer; write exactly frames*channels samples.
    using FillCallback = std::function<void(float* interleaved, std::uint32_t frames)>;

    struct ClockSample {
        double positionSeconds = 0.0; // device playback position
        std::int64_t qpc = 0;         // QueryPerformanceCounter at sampling time
    };

    static Expected<std::unique_ptr<AudioOutput>, std::string> create();
    ~AudioOutput();

    Expected<void, std::string> start(FillCallback fill);
    void stop();

    [[nodiscard]] std::uint32_t sampleRate() const;
    [[nodiscard]] std::uint32_t channels() const;
    [[nodiscard]] std::uint32_t underruns() const;

    [[nodiscard]] Expected<ClockSample, std::string> clock() const;

    struct Impl;

private:
    AudioOutput() = default;
    std::unique_ptr<Impl> impl_;
};

} // namespace velocity::audio
