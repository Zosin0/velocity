#pragma once
// Phase-2 seed of the SequenceCompiler (docs/02 §6): resolves what the
// timeline says should be seen/heard at a given tick. The full FrameGraph
// (effects, transitions, compositing) grows from these entry points.

#include <velocity/engine/model.h>

#include <optional>
#include <vector>

namespace velocity::engine {

struct VideoSample {
    std::filesystem::path asset;
    std::int64_t srcPts = 0; // exact source pts to display, stream timebase
    Rational srcTimebase{1, kTickRate};
    ClipId clip = 0;
};

// Topmost video-track clip covering `at`; nullopt = black/silence gap.
std::optional<VideoSample> resolveVideoAt(const Sequence& seq, Tick at);

struct AudioSegment {
    std::filesystem::path asset;
    std::int64_t srcStartPts = 0; // source position corresponding to `start`
    Rational srcTimebase{1, kTickRate};
    Tick start = 0; // timeline range covered (clipped to the query range)
    Tick len = 0;
    ClipId clip = 0;
};

// All audio-clip pieces intersecting [start, start+len), across audio tracks,
// each clipped to the range. Silence gaps are implicit.
std::vector<AudioSegment> audioSegmentsInRange(const Sequence& seq, Tick start, Tick len);

} // namespace velocity::engine
