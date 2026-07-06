#pragma once
// Phase-0 smoke surface for the FFmpeg integration: proves headers, import
// libraries, and runtime DLLs all agree. The real abstraction layer
// (probe/decode/encode per docs/04) builds on top of this in later phases.

#include <cstdint>
#include <string>

namespace velocity::media {

struct FFmpegVersions {
    unsigned avutil = 0;
    unsigned avcodec = 0;
    unsigned avformat = 0;
    std::string license; // license string reported by the runtime DLLs
};

// Queries the loaded FFmpeg runtime (not the compile-time headers).
FFmpegVersions runtimeVersions();

// True when the runtime DLL major versions match the headers we compiled
// against — the invariant the Phase-0 smoke test enforces.
bool runtimeMatchesHeaders();

} // namespace velocity::media
