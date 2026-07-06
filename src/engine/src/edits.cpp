#include "velocity/engine/edits.h"

#include <algorithm>
#include <atomic>

namespace velocity::engine {

ClipId nextClipId() {
    static std::atomic<ClipId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

namespace {

// Rebuilds one track inside a copied sequence, sharing all other tracks.
// mutate receives a mutable copy of the track's clip vector.
template <typename F>
EditResult withTrack(const SnapshotPtr& seq, size_t trackIdx, F&& mutate) {
    if (trackIdx >= seq->tracks.size())
        return makeUnexpected(std::string("track index out of range"));

    std::vector<ClipPtr> clips = seq->tracks[trackIdx]->clips; // copy of ptr vector
    auto err = mutate(clips);
    if (!err.empty())
        return makeUnexpected(std::move(err));

    std::sort(clips.begin(), clips.end(),
              [](const ClipPtr& a, const ClipPtr& b) { return a->dstStart < b->dstStart; });
    for (size_t i = 1; i < clips.size(); ++i)
        if (clips[i]->dstStart < clips[i - 1]->dstEnd())
            return makeUnexpected(std::string("edit would overlap clips"));
    for (const auto& c : clips)
        if (c->dstLen <= 0)
            return makeUnexpected(std::string("edit would produce empty clip"));

    auto track = std::make_shared<Track>(*seq->tracks[trackIdx]);
    track->clips = std::move(clips);
    auto next = std::make_shared<Sequence>(*seq);
    next->tracks[trackIdx] = std::move(track);
    return SnapshotPtr{std::move(next)};
}

std::vector<ClipPtr>::iterator findClip(std::vector<ClipPtr>& clips, ClipId id) {
    return std::find_if(clips.begin(), clips.end(),
                        [id](const ClipPtr& c) { return c->id == id; });
}

} // namespace

SnapshotPtr makeSequence(int width, int height, Rational fps, int videoTracks,
                         int audioTracks) {
    auto seq = std::make_shared<Sequence>();
    seq->width = width;
    seq->height = height;
    seq->frameRate = fps;
    for (int i = 0; i < videoTracks; ++i) {
        auto t = std::make_shared<Track>();
        t->kind = TrackKind::video;
        t->name = "V" + std::to_string(i + 1);
        seq->tracks.push_back(std::move(t));
    }
    for (int i = 0; i < audioTracks; ++i) {
        auto t = std::make_shared<Track>();
        t->kind = TrackKind::audio;
        t->name = "A" + std::to_string(i + 1);
        seq->tracks.push_back(std::move(t));
    }
    return seq;
}

EditResult addClip(const SnapshotPtr& seq, size_t trackIdx, Clip clip) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        if (clip.id == 0)
            clip.id = nextClipId();
        clips.push_back(std::make_shared<const Clip>(std::move(clip)));
        return {};
    });
}

EditResult splitClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick at) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        const Clip& c = **it;
        if (at <= c.dstStart || at >= c.dstEnd())
            return "split point not strictly inside clip";

        Clip left = c;
        left.dstLen = at - c.dstStart;

        Clip right = c;
        right.id = nextClipId();
        right.dstStart = at;
        right.dstLen = c.dstEnd() - at;
        right.srcStartPts = c.srcStartPts + ptsFromTicks(at - c.dstStart, c.srcTimebase);

        *it = std::make_shared<const Clip>(std::move(left));
        clips.push_back(std::make_shared<const Clip>(std::move(right)));
        return {};
    });
}

EditResult removeClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        clips.erase(it);
        return {};
    });
}

EditResult moveClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newStart) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        if (newStart < 0)
            return "clip cannot start before 0";
        Clip moved = **it;
        moved.dstStart = newStart;
        *it = std::make_shared<const Clip>(std::move(moved));
        return {};
    });
}

EditResult trimClipHead(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newStart) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        const Clip& c = **it;
        if (newStart < 0 || newStart >= c.dstEnd())
            return "invalid head trim";
        Clip trimmed = c;
        trimmed.dstStart = newStart;
        trimmed.dstLen = c.dstEnd() - newStart;
        trimmed.srcStartPts = c.srcStartPts + ptsFromTicks(newStart - c.dstStart, c.srcTimebase);
        *it = std::make_shared<const Clip>(std::move(trimmed));
        return {};
    });
}

EditResult trimClipTail(const SnapshotPtr& seq, size_t trackIdx, ClipId id, Tick newEnd) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        const Clip& c = **it;
        if (newEnd <= c.dstStart)
            return "invalid tail trim";
        Clip trimmed = c;
        trimmed.dstLen = newEnd - c.dstStart;
        *it = std::make_shared<const Clip>(std::move(trimmed));
        return {};
    });
}

} // namespace velocity::engine
