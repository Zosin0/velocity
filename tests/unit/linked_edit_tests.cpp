// Link-group edits (A/V sync contract): split/move/trim/delete act on every
// active member so a cut video can never leave its audio playing, plus track
// management (mute/hide/lock/gain) and image-clip source resolution.

#include <velocity/engine/compile.h>
#include <velocity/engine/edits.h>

#include <gtest/gtest.h>

using namespace velocity;
using namespace velocity::engine;

namespace {
constexpr Tick kSec = kTickRate;

Clip makeClip(const char* asset, Tick dstStart, Tick dstLen, ClipKind kind = ClipKind::video,
              LinkGroupId group = 0) {
    Clip c;
    c.asset = asset;
    c.kind = kind;
    c.linkGroup = group;
    c.dstStart = dstStart;
    c.dstLen = dstLen;
    c.srcTimebase = {1, kTickRate};
    return c;
}

// One video + one linked audio clip from the same "file", 0..4s.
SnapshotPtr makeLinkedAv(LinkGroupId& groupOut) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 1);
    groupOut = nextLinkGroup();
    auto s = addClip(seq, 0, makeClip("av.mp4", 0, 4 * kSec, ClipKind::video, groupOut));
    s = addClip(*s, 1, makeClip("av.mp4", 0, 4 * kSec, ClipKind::audio, groupOut));
    EXPECT_TRUE(s.hasValue());
    return *s;
}
} // namespace

TEST(LinkedEdits, SplitCutsVideoAndAudioTogether) {
    LinkGroupId group = 0;
    auto seq = makeLinkedAv(group);
    const ClipId videoId = seq->tracks[0]->clips[0]->id;

    auto cut = splitClipLinked(seq, videoId, kSec);
    ASSERT_TRUE(cut.hasValue()) << cut.error();

    // Both tracks now hold two clips cut at the same tick.
    for (size_t t = 0; t < 2; ++t) {
        const auto& clips = (*cut)->tracks[t]->clips;
        ASSERT_EQ(clips.size(), 2u) << "track " << t;
        EXPECT_EQ(clips[0]->dstEnd(), kSec);
        EXPECT_EQ(clips[1]->dstStart, kSec);
    }
    // Left halves keep the original group; right halves share a NEW group so
    // each side still edits as one A/V unit.
    const auto& v = (*cut)->tracks[0]->clips;
    const auto& a = (*cut)->tracks[1]->clips;
    EXPECT_EQ(v[0]->linkGroup, group);
    EXPECT_EQ(a[0]->linkGroup, group);
    EXPECT_EQ(v[1]->linkGroup, a[1]->linkGroup);
    EXPECT_NE(v[1]->linkGroup, group);
}

TEST(LinkedEdits, DeleteRemovesWholeGroupSoNoOrphanAudio) {
    LinkGroupId group = 0;
    auto seq = makeLinkedAv(group);
    const ClipId videoId = seq->tracks[0]->clips[0]->id;

    // The export-facing regression: cut at 1s, delete the right half, and the
    // audio past 1s must be gone from the mix too.
    auto cut = splitClipLinked(seq, videoId, kSec);
    ASSERT_TRUE(cut.hasValue());
    const ClipId rightVideo = (*cut)->tracks[0]->clips[1]->id;

    auto del = removeClipLinked(*cut, rightVideo);
    ASSERT_TRUE(del.hasValue()) << del.error();

    EXPECT_EQ((*del)->tracks[0]->clips.size(), 1u);
    EXPECT_EQ((*del)->tracks[1]->clips.size(), 1u);
    EXPECT_EQ((*del)->duration(), kSec);
    // Nothing audible after the cut point.
    EXPECT_TRUE(audioSegmentsInRange(**del, kSec, 3 * kSec).empty());
}

TEST(LinkedEdits, MoveKeepsAvInSync) {
    LinkGroupId group = 0;
    auto seq = makeLinkedAv(group);
    const ClipId videoId = seq->tracks[0]->clips[0]->id;

    auto moved = moveClipLinked(seq, videoId, 2 * kSec);
    ASSERT_TRUE(moved.hasValue()) << moved.error();
    EXPECT_EQ((*moved)->tracks[0]->clips[0]->dstStart, 2 * kSec);
    EXPECT_EQ((*moved)->tracks[1]->clips[0]->dstStart, 2 * kSec);
}

TEST(LinkedEdits, TrimFollowsSharedEdges) {
    LinkGroupId group = 0;
    auto seq = makeLinkedAv(group);
    const ClipId videoId = seq->tracks[0]->clips[0]->id;

    auto head = trimClipLinkedHead(seq, videoId, kSec / 2);
    ASSERT_TRUE(head.hasValue());
    EXPECT_EQ((*head)->tracks[0]->clips[0]->dstStart, kSec / 2);
    EXPECT_EQ((*head)->tracks[1]->clips[0]->dstStart, kSec / 2);

    auto tail = trimClipLinkedTail(*head, videoId, 3 * kSec);
    ASSERT_TRUE(tail.hasValue());
    EXPECT_EQ((*tail)->tracks[0]->clips[0]->dstEnd(), 3 * kSec);
    EXPECT_EQ((*tail)->tracks[1]->clips[0]->dstEnd(), 3 * kSec);
}

TEST(LinkedEdits, DetachMakesClipsIndependent) {
    LinkGroupId group = 0;
    auto seq = makeLinkedAv(group);
    const ClipId videoId = seq->tracks[0]->clips[0]->id;
    const ClipId audioId = seq->tracks[1]->clips[0]->id;

    auto detached = detachLink(seq, videoId);
    ASSERT_TRUE(detached.hasValue()) << detached.error();

    // Sync metadata survives, the behavior does not.
    EXPECT_EQ((*detached)->tracks[0]->clips[0]->linkGroup, group);
    EXPECT_TRUE((*detached)->tracks[0]->clips[0]->linkDetached);
    EXPECT_EQ(linkedMembers(**detached, videoId).size(), 1u);

    // Audio now deletes without touching the video, and vice versa.
    auto delAudio = removeClipLinked(*detached, audioId);
    ASSERT_TRUE(delAudio.hasValue());
    EXPECT_EQ((*delAudio)->tracks[0]->clips.size(), 1u);
    EXPECT_TRUE((*delAudio)->tracks[1]->clips.empty());

    // And the audio moves independently.
    auto moveAudio = moveClipLinked(*detached, audioId, 2 * kSec);
    ASSERT_TRUE(moveAudio.hasValue());
    EXPECT_EQ((*moveAudio)->tracks[0]->clips[0]->dstStart, 0);
    EXPECT_EQ((*moveAudio)->tracks[1]->clips[0]->dstStart, 2 * kSec);

    // Detaching an unlinked clip is a no-op error.
    EXPECT_FALSE(detachLink(*delAudio, videoId).hasValue());
}

TEST(Tracks, AddRemoveAndAutoNames) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 2, 1);
    auto s = addTrack(seq, TrackKind::video);
    ASSERT_TRUE(s.hasValue());
    // New video track slots after the existing video tracks, before audio.
    ASSERT_EQ((*s)->tracks.size(), 4u);
    EXPECT_EQ((*s)->tracks[2]->kind, TrackKind::video);
    EXPECT_EQ((*s)->tracks[2]->name, "V3");
    EXPECT_EQ((*s)->tracks[3]->kind, TrackKind::audio);

    auto s2 = addTrack(*s, TrackKind::audio);
    ASSERT_TRUE(s2.hasValue());
    EXPECT_EQ((*s2)->tracks.back()->name, "A2");

    auto removed = removeTrack(*s2, 2);
    ASSERT_TRUE(removed.hasValue());
    EXPECT_EQ((*removed)->tracks.size(), 4u);
    EXPECT_FALSE(removeTrack(*removed, 99).hasValue());
}

TEST(Tracks, LockedTrackRejectsClipEditsButNotTrackEdits) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    auto s = addClip(seq, 0, makeClip("a.mp4", 0, kSec));
    ASSERT_TRUE(s.hasValue());
    const ClipId id = (*s)->tracks[0]->clips[0]->id;

    auto locked = updateTrack(*s, 0, [](Track& t) { t.locked = true; });
    ASSERT_TRUE(locked.hasValue());

    EXPECT_FALSE(addClip(*locked, 0, makeClip("b.mp4", 2 * kSec, kSec)).hasValue());
    EXPECT_FALSE(moveClip(*locked, 0, id, kSec).hasValue());
    EXPECT_FALSE(removeClip(*locked, 0, id).hasValue());
    EXPECT_FALSE(trimClipTail(*locked, 0, id, kSec / 2).hasValue());

    // Unlocking through updateTrack must still work.
    auto unlocked = updateTrack(*locked, 0, [](Track& t) { t.locked = false; });
    ASSERT_TRUE(unlocked.hasValue());
    EXPECT_TRUE(moveClip(*unlocked, 0, id, kSec).hasValue());
}

TEST(Tracks, HiddenVideoAndMutedAudioAreExcluded) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 2, 1);
    auto s = addClip(seq, 0, makeClip("base.mp4", 0, 2 * kSec));
    s = addClip(*s, 1, makeClip("overlay.mp4", 0, 2 * kSec));
    s = addClip(*s, 2, makeClip("music.mp3", 0, 2 * kSec, ClipKind::audio));
    ASSERT_TRUE(s.hasValue());

    // Hide the overlay's track: the base shows through both resolvers.
    auto hidden = updateTrack(*s, 1, [](Track& t) { t.hidden = true; });
    ASSERT_TRUE(hidden.hasValue());
    EXPECT_EQ(resolveVideoAt(**hidden, kSec)->asset, "base.mp4");
    EXPECT_EQ(resolveVideoLayersAt(**hidden, kSec).size(), 1u);

    // Mute the audio track: no segments at all.
    auto muted = updateTrack(*s, 2, [](Track& t) { t.muted = true; });
    ASSERT_TRUE(muted.hasValue());
    EXPECT_TRUE(audioSegmentsInRange(**muted, 0, 2 * kSec).empty());
}

TEST(Tracks, TrackGainMultipliesClipGain) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 0, 1);
    Clip c = makeClip("music.mp3", 0, kSec, ClipKind::audio);
    c.gain = 0.5f;
    auto s = addClip(seq, 0, std::move(c));
    ASSERT_TRUE(s.hasValue());

    auto faded = updateTrack(*s, 0, [](Track& t) { t.gain = 0.5f; });
    ASSERT_TRUE(faded.hasValue());
    auto segs = audioSegmentsInRange(**faded, 0, kSec);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_FLOAT_EQ(segs[0].gain, 0.25f);
}

TEST(Compile, ImageClipsAlwaysResolveTheirFirstFrame) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    auto s = addClip(seq, 0, makeClip("logo.png", 0, 5 * kSec, ClipKind::image));
    ASSERT_TRUE(s.hasValue());

    // Anywhere in the clip, the source position stays at the head: a single
    // decoded frame serves the whole clip (no per-playhead re-seek).
    for (Tick at : {Tick{0}, kSec, 3 * kSec, 5 * kSec - 1}) {
        auto v = resolveVideoAt(**s, at);
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(v->srcPts, 0);
    }
}
