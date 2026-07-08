#include "velocity/engine/edits.h"

#include <algorithm>
#include <atomic>

namespace velocity::engine {

ClipId nextClipId() {
    static std::atomic<ClipId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

LinkGroupId nextLinkGroup() {
    static std::atomic<LinkGroupId> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

namespace {

// Rebuilds one track inside a copied sequence, sharing all other tracks.
// mutate receives a mutable copy of the track's clip vector.
template <typename F>
EditResult withTrack(const SnapshotPtr& seq, size_t trackIdx, F&& mutate) {
    if (trackIdx >= seq->tracks.size())
        return makeUnexpected(std::string("track index out of range"));
    if (seq->tracks[trackIdx]->locked)
        return makeUnexpected(std::string("track is locked"));

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

EditResult updateClip(const SnapshotPtr& seq, size_t trackIdx, ClipId id,
                      const std::function<void(Clip&)>& mutate) {
    return withTrack(seq, trackIdx, [&](std::vector<ClipPtr>& clips) -> std::string {
        auto it = findClip(clips, id);
        if (it == clips.end())
            return "clip not found";
        Clip changed = **it;
        mutate(changed);
        changed.id = id; // identity is immutable
        *it = std::make_shared<const Clip>(std::move(changed));
        return {};
    });
}

EditResult moveClipToTrack(const SnapshotPtr& seq, size_t fromTrack, ClipId id, size_t toTrack,
                           Tick newStart) {
    if (fromTrack == toTrack)
        return moveClip(seq, fromTrack, id, newStart);
    if (fromTrack >= seq->tracks.size() || toTrack >= seq->tracks.size())
        return makeUnexpected(std::string("track index out of range"));
    if (seq->tracks[fromTrack]->kind != seq->tracks[toTrack]->kind)
        return makeUnexpected(std::string("cannot move clip between video and audio tracks"));
    if (newStart < 0)
        return makeUnexpected(std::string("clip cannot start before 0"));

    // Find and detach the clip from the source track.
    ClipPtr moved;
    for (const auto& c : seq->tracks[fromTrack]->clips)
        if (c->id == id)
            moved = c;
    if (!moved)
        return makeUnexpected(std::string("clip not found"));

    auto removed = removeClip(seq, fromTrack, id);
    if (!removed)
        return removed;

    Clip placed = *moved;
    placed.dstStart = newStart;
    return addClip(*removed, toTrack, std::move(placed));
}

namespace {
// Locates a clip anywhere in the sequence. Returns {trackIdx, clip} or
// {SIZE_MAX, nullptr} when absent.
std::pair<size_t, ClipPtr> locateClip(const Sequence& seq, ClipId id) {
    for (size_t t = 0; t < seq.tracks.size(); ++t)
        for (const auto& c : seq.tracks[t]->clips)
            if (c->id == id)
                return {t, c};
    return {static_cast<size_t>(-1), nullptr};
}
} // namespace

std::vector<std::pair<size_t, ClipPtr>> linkedMembers(const Sequence& seq, ClipId id) {
    std::vector<std::pair<size_t, ClipPtr>> out;
    auto [trackIdx, clip] = locateClip(seq, id);
    if (!clip)
        return out;
    if (!clip->isLinked()) {
        out.emplace_back(trackIdx, clip);
        return out;
    }
    for (size_t t = 0; t < seq.tracks.size(); ++t)
        for (const auto& c : seq.tracks[t]->clips)
            if (c->linkGroup == clip->linkGroup && !c->linkDetached)
                out.emplace_back(t, c);
    return out;
}

EditResult splitClipLinked(const SnapshotPtr& seq, ClipId id, Tick at) {
    auto members = linkedMembers(*seq, id);
    if (members.empty())
        return makeUnexpected(std::string("clip not found"));

    // Split every member whose body strictly contains the cut. The right
    // halves form a fresh group so each side keeps editing as one A/V unit.
    const LinkGroupId rightGroup =
        members.size() > 1 ? nextLinkGroup() : members.front().second->linkGroup;

    SnapshotPtr cur = seq;
    bool any = false;
    for (const auto& [trackIdx, clip] : members) {
        if (at <= clip->dstStart || at >= clip->dstEnd())
            continue;
        auto next = withTrack(cur, trackIdx, [&, clipPtr = clip](std::vector<ClipPtr>& clips) -> std::string {
            auto it = findClip(clips, clipPtr->id);
            if (it == clips.end())
                return "clip not found";
            const Clip& c = **it;

            Clip left = c;
            left.dstLen = at - c.dstStart;

            Clip right = c;
            right.id = nextClipId();
            right.linkGroup = rightGroup;
            right.dstStart = at;
            right.dstLen = c.dstEnd() - at;
            right.srcStartPts = c.srcStartPts + ptsFromTicks(at - c.dstStart, c.srcTimebase);

            *it = std::make_shared<const Clip>(std::move(left));
            clips.push_back(std::make_shared<const Clip>(std::move(right)));
            return {};
        });
        if (!next)
            return next;
        cur = std::move(next.value());
        any = true;
    }
    if (!any)
        return makeUnexpected(std::string("split point not strictly inside clip"));
    return cur;
}

EditResult removeClipLinked(const SnapshotPtr& seq, ClipId id) {
    auto members = linkedMembers(*seq, id);
    if (members.empty())
        return makeUnexpected(std::string("clip not found"));
    SnapshotPtr cur = seq;
    for (const auto& [trackIdx, clip] : members) {
        auto next = removeClip(cur, trackIdx, clip->id);
        if (!next)
            return next;
        cur = std::move(next.value());
    }
    return cur;
}

EditResult moveClipLinked(const SnapshotPtr& seq, ClipId id, Tick newStart) {
    auto [anchorTrack, anchor] = locateClip(*seq, id);
    if (!anchor)
        return makeUnexpected(std::string("clip not found"));
    const Tick delta = newStart - anchor->dstStart;
    SnapshotPtr cur = seq;
    for (const auto& [trackIdx, clip] : linkedMembers(*seq, id)) {
        auto next = moveClip(cur, trackIdx, clip->id, clip->dstStart + delta);
        if (!next)
            return next;
        cur = std::move(next.value());
    }
    return cur;
}

EditResult trimClipLinkedHead(const SnapshotPtr& seq, ClipId id, Tick newStart) {
    auto [anchorTrack, anchor] = locateClip(*seq, id);
    if (!anchor)
        return makeUnexpected(std::string("clip not found"));
    SnapshotPtr cur = seq;
    for (const auto& [trackIdx, clip] : linkedMembers(*seq, id)) {
        if (clip->dstStart != anchor->dstStart)
            continue; // only members sharing the trimmed edge follow it
        auto next = trimClipHead(cur, trackIdx, clip->id, newStart);
        if (!next)
            return next;
        cur = std::move(next.value());
    }
    return cur;
}

EditResult trimClipLinkedTail(const SnapshotPtr& seq, ClipId id, Tick newEnd) {
    auto [anchorTrack, anchor] = locateClip(*seq, id);
    if (!anchor)
        return makeUnexpected(std::string("clip not found"));
    SnapshotPtr cur = seq;
    for (const auto& [trackIdx, clip] : linkedMembers(*seq, id)) {
        if (clip->dstEnd() != anchor->dstEnd())
            continue;
        auto next = trimClipTail(cur, trackIdx, clip->id, newEnd);
        if (!next)
            return next;
        cur = std::move(next.value());
    }
    return cur;
}

EditResult detachLink(const SnapshotPtr& seq, ClipId id) {
    auto members = linkedMembers(*seq, id);
    if (members.empty())
        return makeUnexpected(std::string("clip not found"));
    if (members.size() < 2)
        return makeUnexpected(std::string("clip has no linked companion"));
    SnapshotPtr cur = seq;
    for (const auto& [trackIdx, clip] : members) {
        auto next = updateClip(cur, trackIdx, clip->id,
                               [](Clip& c) { c.linkDetached = true; });
        if (!next)
            return next;
        cur = std::move(next.value());
    }
    return cur;
}

EditResult addTrack(const SnapshotPtr& seq, TrackKind kind) {
    auto next = std::make_shared<Sequence>(*seq);
    int count = 0;
    size_t insertAt = 0; // video tracks stay grouped above the audio tracks
    for (size_t i = 0; i < next->tracks.size(); ++i) {
        if (next->tracks[i]->kind == kind)
            ++count;
        if (kind == TrackKind::video ? next->tracks[i]->kind == TrackKind::video : true)
            insertAt = i + 1;
    }
    auto track = std::make_shared<Track>();
    track->kind = kind;
    track->name = (kind == TrackKind::video ? "V" : "A") + std::to_string(count + 1);
    next->tracks.insert(next->tracks.begin() + static_cast<std::ptrdiff_t>(insertAt),
                        std::move(track));
    return SnapshotPtr{std::move(next)};
}

EditResult removeTrack(const SnapshotPtr& seq, size_t trackIdx) {
    if (trackIdx >= seq->tracks.size())
        return makeUnexpected(std::string("track index out of range"));
    if (seq->tracks[trackIdx]->locked)
        return makeUnexpected(std::string("track is locked"));
    auto next = std::make_shared<Sequence>(*seq);
    next->tracks.erase(next->tracks.begin() + static_cast<std::ptrdiff_t>(trackIdx));
    return SnapshotPtr{std::move(next)};
}

EditResult updateTrack(const SnapshotPtr& seq, size_t trackIdx,
                       const std::function<void(Track&)>& mutate) {
    if (trackIdx >= seq->tracks.size())
        return makeUnexpected(std::string("track index out of range"));
    auto track = std::make_shared<Track>(*seq->tracks[trackIdx]);
    const TrackKind kind = track->kind;
    auto clips = track->clips;
    mutate(*track);
    track->kind = kind;             // identity is immutable
    track->clips = std::move(clips); // property edits cannot touch clips
    auto next = std::make_shared<Sequence>(*seq);
    next->tracks[trackIdx] = std::move(track);
    return SnapshotPtr{std::move(next)};
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
