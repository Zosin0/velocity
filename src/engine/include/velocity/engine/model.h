#pragma once
// The timeline model (docs/02 §2, §4 — reduced to Phase-2 scope).
// LOAD-BEARING INVARIANTS, do not weaken:
//   * Every node is immutable after construction. Edits build new nodes,
//     sharing unchanged children (structural sharing via shared_ptr-to-const).
//   * All timeline positions/durations are integer Ticks (1/48000 s).
//     Source-media positions are integers in the stream's own timebase.
//   * Clips within one track are sorted by dstStart and never overlap.
// Engines hold a SnapshotPtr and read it lock-free; publishing a new snapshot
// is a shared_ptr assignment in the owning (UI/session) thread.

#include <velocity/foundation/time.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace velocity::engine {

using ClipId = std::uint64_t;
ClipId nextClipId(); // process-wide monotonic

using LinkGroupId = std::uint64_t;
LinkGroupId nextLinkGroup(); // process-wide monotonic, 0 = unlinked

enum class TrackKind { video, audio };

// What a clip's asset fundamentally is. Image clips have a single source
// frame: resolvers pin their source pts to 0 and their duration is free.
enum class ClipKind { video, audio, image };

// Static (non-animated) per-clip video transform. Normalized center offsets:
// (0,0) = frame center; ±0.5 = half the sequence dimension. Keyframing is a
// later phase; the fields are value types so that upgrade is additive.
struct ClipTransform {
    float posX = 0.0f;
    float posY = 0.0f;
    float scale = 1.0f;
    float rotation = 0.0f; // degrees
    float opacity = 1.0f;

    [[nodiscard]] bool isIdentity() const {
        return posX == 0.0f && posY == 0.0f && scale == 1.0f && rotation == 0.0f &&
               opacity == 1.0f;
    }
};

struct Clip {
    ClipId id = 0;
    std::filesystem::path asset; // Phase 2: the file path is the asset identity
    ClipKind kind = ClipKind::video;

    // A/V link: clips imported from one file share a nonzero linkGroup and
    // edit together (split/move/trim/delete). "Detach audio" sets
    // linkDetached, which suspends the behavior but keeps the group id as
    // sync metadata for future relinking.
    LinkGroupId linkGroup = 0;
    bool linkDetached = false;

    Tick dstStart = 0; // placement on the timeline
    Tick dstLen = 0;

    std::int64_t srcStartPts = 0; // in-point in the source stream's timebase
    Rational srcTimebase{1, kTickRate};

    // Audio properties (audio clips and video-with-audio).
    float gain = 1.0f; // linear
    bool mute = false;
    Tick fadeIn = 0;  // duration from clip head
    Tick fadeOut = 0; // duration to clip tail

    // Video properties (video/image/title clips).
    ClipTransform transform;
    bool hidden = false;

    [[nodiscard]] Tick dstEnd() const { return dstStart + dstLen; }
    [[nodiscard]] bool contains(Tick t) const { return t >= dstStart && t < dstEnd(); }
    [[nodiscard]] bool isLinked() const { return linkGroup != 0 && !linkDetached; }
};

using ClipPtr = std::shared_ptr<const Clip>;

struct Track {
    TrackKind kind = TrackKind::video;
    std::string name;
    bool muted = false;  // audio tracks: excluded from the mix
    bool hidden = false; // video tracks: excluded from resolve/composite
    bool locked = false; // clip edits on this track are rejected
    float gain = 1.0f;   // audio tracks: fader applied on top of clip gain
    std::vector<ClipPtr> clips; // sorted by dstStart, non-overlapping
};

using TrackPtr = std::shared_ptr<const Track>;

struct Sequence {
    int width = 1920;
    int height = 1080;
    Rational frameRate{30, 1};
    int audioRate = 48000; // engine rate; ticks map 1:1 to samples
    std::vector<TrackPtr> tracks;

    [[nodiscard]] Tick duration() const {
        Tick end = 0;
        for (const auto& t : tracks)
            if (!t->clips.empty())
                end = std::max(end, t->clips.back()->dstEnd());
        return end;
    }
};

using SnapshotPtr = std::shared_ptr<const Sequence>;

} // namespace velocity::engine
