#pragma once
// Export engine (docs/10, single primary path): renders a timeline snapshot
// to MP4 H.264+AAC as fast as decode/encode allow. Same resolvers as
// playback — "what you previewed is what you export" (docs/10 §1).
//
// Runs synchronously on the calling thread; callers (UI) run it on a worker
// thread and use the progress callback for UI updates and cancellation.

#include <velocity/engine/model.h>
#include <velocity/foundation/expected.h>

#include <filesystem>
#include <functional>
#include <string>

namespace velocity::engine {

struct ExportSettings {
    int width = 0;      // 0 → sequence width
    int height = 0;     // 0 → sequence height
    Rational fps{0, 1}; // 0 → sequence rate
    std::int64_t videoBitrate = 8'000'000;
    std::int64_t audioBitrate = 192'000;
    bool preferHardwareEncoder = true;
    float masterGain = 1.0f; // master fader applied after the track sum
};

struct ExportResult {
    std::int64_t videoFrames = 0;
    std::int64_t audioSamples = 0;
    std::string videoEncoder; // e.g. "h264_nvenc" or "libopenh264"
};

// Called periodically with progress in [0,1]; return false to cancel.
using ExportProgressFn = std::function<bool(double)>;

// Expected number of output frames for a sequence at rate fps — the doc-10
// frame-count gate compares exports against this exact value.
std::int64_t expectedFrameCount(Tick duration, Rational fps);

Expected<ExportResult, std::string>
exportSequence(const SnapshotPtr& seq, const std::filesystem::path& outFile,
               const ExportSettings& settings = {}, const ExportProgressFn& progress = {});

} // namespace velocity::engine
