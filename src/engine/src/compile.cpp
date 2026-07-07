#include "velocity/engine/compile.h"

#include <algorithm>

namespace velocity::engine {

namespace {
// Binary search: the clip containing `at`, or nullptr.
const Clip* clipAt(const Track& track, Tick at) {
    auto it = std::upper_bound(track.clips.begin(), track.clips.end(), at,
                               [](Tick t, const ClipPtr& c) { return t < c->dstStart; });
    if (it == track.clips.begin())
        return nullptr;
    const Clip* c = std::prev(it)->get();
    return c->contains(at) ? c : nullptr;
}
} // namespace

std::optional<VideoSample> resolveVideoAt(const Sequence& seq, Tick at) {
    // Later-indexed video tracks render on top (docs/11); resolve top-down.
    for (auto it = seq.tracks.rbegin(); it != seq.tracks.rend(); ++it) {
        if ((*it)->kind != TrackKind::video)
            continue;
        if (const Clip* c = clipAt(**it, at)) {
            if (c->hidden || c->transform.opacity <= 0.0f)
                continue; // invisible: the track below shows through
            VideoSample s;
            s.asset = c->asset;
            s.srcPts = c->srcStartPts + ptsFromTicks(at - c->dstStart, c->srcTimebase);
            s.srcTimebase = c->srcTimebase;
            s.clip = c->id;
            s.transform = c->transform;
            return s;
        }
    }
    return std::nullopt;
}

std::vector<AudioSegment> audioSegmentsInRange(const Sequence& seq, Tick start, Tick len) {
    std::vector<AudioSegment> out;
    const Tick end = start + len;
    for (const auto& track : seq.tracks) {
        if (track->kind != TrackKind::audio)
            continue;
        for (const auto& c : track->clips) {
            if (c->mute)
                continue;
            const Tick s = std::max(start, c->dstStart);
            const Tick e = std::min(end, c->dstEnd());
            if (s >= e)
                continue;
            AudioSegment seg;
            seg.asset = c->asset;
            seg.srcStartPts = c->srcStartPts + ptsFromTicks(s - c->dstStart, c->srcTimebase);
            seg.srcTimebase = c->srcTimebase;
            seg.start = s;
            seg.len = e - s;
            seg.clip = c->id;
            seg.gain = c->gain;
            seg.clipStart = c->dstStart;
            seg.clipEnd = c->dstEnd();
            seg.fadeIn = c->fadeIn;
            seg.fadeOut = c->fadeOut;
            out.push_back(std::move(seg));
        }
    }
    return out;
}

} // namespace velocity::engine
