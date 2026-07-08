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

namespace {
// Image clips have exactly one source frame; their source position is always
// the head of the stream regardless of where the playhead sits in the clip.
std::int64_t sourcePtsAt(const Clip& c, Tick at) {
    if (c.kind == ClipKind::image)
        return c.srcStartPts;
    return c.srcStartPts + ptsFromTicks(at - c.dstStart, c.srcTimebase);
}
} // namespace

std::vector<VideoSample> resolveVideoLayersAt(const Sequence& seq, Tick at) {
    std::vector<VideoSample> layers;
    for (const auto& track : seq.tracks) {
        if (track->kind != TrackKind::video || track->hidden)
            continue;
        const Clip* c = clipAt(*track, at);
        if (!c || c->hidden || c->transform.opacity <= 0.0f)
            continue;
        VideoSample s;
        s.asset = c->asset;
        s.srcPts = sourcePtsAt(*c, at);
        s.srcTimebase = c->srcTimebase;
        s.clip = c->id;
        s.transform = c->transform;
        layers.push_back(std::move(s));
    }
    return layers;
}

std::optional<VideoSample> resolveVideoAt(const Sequence& seq, Tick at) {
    // Later-indexed video tracks render on top (docs/11); resolve top-down.
    for (auto it = seq.tracks.rbegin(); it != seq.tracks.rend(); ++it) {
        if ((*it)->kind != TrackKind::video || (*it)->hidden)
            continue;
        if (const Clip* c = clipAt(**it, at)) {
            if (c->hidden || c->transform.opacity <= 0.0f)
                continue; // invisible: the track below shows through
            VideoSample s;
            s.asset = c->asset;
            s.srcPts = sourcePtsAt(*c, at);
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
        if (track->kind != TrackKind::audio || track->muted)
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
            seg.gain = c->gain * track->gain; // clip fader × track fader
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
