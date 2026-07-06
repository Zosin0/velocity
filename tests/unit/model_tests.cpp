// Timeline model invariants (docs/02 §2/§4): immutability + structural
// sharing, overlap rejection, exact split source math, trim semantics,
// undo/redo, and compiler resolution incl. top-track priority.

#include <velocity/engine/compile.h>
#include <velocity/engine/edits.h>

#include <gtest/gtest.h>

using namespace velocity;
using namespace velocity::engine;

namespace {
// One timeline second in ticks.
constexpr Tick kSec = kTickRate;

Clip makeClip(const char* asset, Tick dstStart, Tick dstLen, std::int64_t srcStart = 0,
              Rational tb = {1, 30000}) {
    Clip c;
    c.asset = asset;
    c.dstStart = dstStart;
    c.dstLen = dstLen;
    c.srcStartPts = srcStart;
    c.srcTimebase = tb;
    return c;
}
} // namespace

TEST(Model, AddClipRejectsOverlap) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 1);
    auto s1 = addClip(seq, 0, makeClip("a.mp4", 0, 2 * kSec));
    ASSERT_TRUE(s1.hasValue());
    // Exactly adjacent is fine.
    auto s2 = addClip(*s1, 0, makeClip("b.mp4", 2 * kSec, kSec));
    ASSERT_TRUE(s2.hasValue()) << s2.error();
    // One tick of overlap is not.
    auto bad = addClip(*s2, 0, makeClip("c.mp4", 3 * kSec - 1, kSec));
    EXPECT_FALSE(bad.hasValue());
}

TEST(Model, StructuralSharingAndImmutability) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 2, 2);
    auto s1 = addClip(seq, 0, makeClip("a.mp4", 0, kSec));
    ASSERT_TRUE(s1.hasValue());
    // Original snapshot untouched.
    EXPECT_TRUE(seq->tracks[0]->clips.empty());
    EXPECT_EQ((*s1)->tracks[0]->clips.size(), 1u);
    // Unmodified tracks are the same shared nodes, not copies.
    for (size_t i = 1; i < seq->tracks.size(); ++i)
        EXPECT_EQ(seq->tracks[i].get(), (*s1)->tracks[i].get());
}

TEST(Model, SplitPreservesTimingAndSourceContinuity) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    // Clip starts at 10s on the timeline, 90 ticks of source offset, NTSC tb.
    auto s1 = addClip(seq, 0, makeClip("a.mp4", 10 * kSec, 4 * kSec, 3003, {1001, 30000}));
    ASSERT_TRUE(s1.hasValue());
    const ClipId id = (*s1)->tracks[0]->clips[0]->id;

    const Tick cut = 11 * kSec; // 1s into the clip
    auto s2 = splitClip(*s1, 0, id, cut);
    ASSERT_TRUE(s2.hasValue()) << s2.error();
    const auto& clips = (*s2)->tracks[0]->clips;
    ASSERT_EQ(clips.size(), 2u);

    EXPECT_EQ(clips[0]->dstStart, 10 * kSec);
    EXPECT_EQ(clips[0]->dstEnd(), cut);
    EXPECT_EQ(clips[1]->dstStart, cut);
    EXPECT_EQ(clips[1]->dstEnd(), 14 * kSec);
    // Total content preserved.
    EXPECT_EQ(clips[0]->dstLen + clips[1]->dstLen, 4 * kSec);
    // Right half's source position == left's start + elapsed, converted
    // exactly into the source timebase: 1s at tb 1001/30000.
    EXPECT_EQ(clips[1]->srcStartPts,
              clips[0]->srcStartPts + ptsFromTicks(kSec, {1001, 30000}));
    // Splitting at a clip edge is rejected.
    EXPECT_FALSE(splitClip(*s2, 0, clips[0]->id, 10 * kSec).hasValue());
}

TEST(Model, TrimHeadAdvancesSource) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    auto s1 = addClip(seq, 0, makeClip("a.mp4", 0, 2 * kSec, 0, {1, 48000}));
    ASSERT_TRUE(s1.hasValue());
    const ClipId id = (*s1)->tracks[0]->clips[0]->id;

    auto s2 = trimClipHead(*s1, 0, id, kSec / 2);
    ASSERT_TRUE(s2.hasValue());
    const auto& c = *(*s2)->tracks[0]->clips[0];
    EXPECT_EQ(c.dstStart, kSec / 2);
    EXPECT_EQ(c.dstEnd(), 2 * kSec);
    EXPECT_EQ(c.srcStartPts, 24000); // 0.5s at 48kHz timebase

    auto s3 = trimClipTail(*s2, 0, id, kSec);
    ASSERT_TRUE(s3.hasValue());
    EXPECT_EQ((*s3)->tracks[0]->clips[0]->dstLen, kSec / 2);
}

TEST(Model, MoveRejectsOverlapAndNegative) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    auto s1 = addClip(seq, 0, makeClip("a.mp4", 0, kSec));
    auto s2 = addClip(*s1, 0, makeClip("b.mp4", 2 * kSec, kSec));
    ASSERT_TRUE(s2.hasValue());
    const ClipId a = (*s2)->tracks[0]->clips[0]->id;

    EXPECT_TRUE(moveClip(*s2, 0, a, kSec).hasValue());          // into the gap: ok
    EXPECT_FALSE(moveClip(*s2, 0, a, 2 * kSec + 10).hasValue()); // onto b: no
    EXPECT_FALSE(moveClip(*s2, 0, a, -5).hasValue());
}

TEST(Model, UndoRedoLinearHistory) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 1, 0);
    UndoStack undo(seq);

    auto s1 = addClip(undo.current(), 0, makeClip("a.mp4", 0, kSec));
    undo.push(*s1);
    auto s2 = addClip(undo.current(), 0, makeClip("b.mp4", kSec, kSec));
    undo.push(*s2);

    EXPECT_EQ(undo.current()->tracks[0]->clips.size(), 2u);
    EXPECT_EQ(undo.undo()->tracks[0]->clips.size(), 1u);
    EXPECT_EQ(undo.undo()->tracks[0]->clips.size(), 0u);
    EXPECT_FALSE(undo.canUndo());
    EXPECT_EQ(undo.redo()->tracks[0]->clips.size(), 1u);
    // New edit after undo drops the redo branch.
    auto s3 = addClip(undo.current(), 0, makeClip("c.mp4", 5 * kSec, kSec));
    undo.push(*s3);
    EXPECT_FALSE(undo.canRedo());
}

TEST(Compile, ResolvesTopTrackAndExactSourcePts) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 2, 1);
    // V1 (track 0): base clip covering 0..4s. V2 (track 1): overlay 1..2s.
    auto s = addClip(seq, 0, makeClip("base.mp4", 0, 4 * kSec, 0, {1001, 30000}));
    s = addClip(*s, 1, makeClip("overlay.mp4", kSec, kSec, 6006, {1001, 30000}));
    ASSERT_TRUE(s.hasValue());

    // Outside overlay: base wins; source pts exact for NTSC timebase.
    auto v = resolveVideoAt(**s, kSec / 2);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->asset, "base.mp4");
    EXPECT_EQ(v->srcPts, ptsFromTicks(kSec / 2, {1001, 30000}));

    // Inside overlay: top track wins, with its own source offset.
    v = resolveVideoAt(**s, kSec + kSec / 4);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->asset, "overlay.mp4");
    EXPECT_EQ(v->srcPts, 6006 + ptsFromTicks(kSec / 4, {1001, 30000}));

    // In a gap: nothing.
    EXPECT_FALSE(resolveVideoAt(**s, 5 * kSec).has_value());
}

TEST(Compile, AudioSegmentsClippedToRange) {
    auto seq = makeSequence(1920, 1080, {30, 1}, 0, 2);
    auto s = addClip(seq, 0, makeClip("voice.wav", 0, 3 * kSec, 0, {1, 48000}));
    s = addClip(*s, 1, makeClip("music.mp3", kSec, 4 * kSec, 96000, {1, 48000}));
    ASSERT_TRUE(s.hasValue());

    // Query 2s..4s: voice contributes its last second, music the middle.
    auto segs = audioSegmentsInRange(**s, 2 * kSec, 2 * kSec);
    ASSERT_EQ(segs.size(), 2u);

    EXPECT_EQ(segs[0].asset, "voice.wav");
    EXPECT_EQ(segs[0].start, 2 * kSec);
    EXPECT_EQ(segs[0].len, kSec);
    EXPECT_EQ(segs[0].srcStartPts, 96000); // 2s at 48kHz

    EXPECT_EQ(segs[1].asset, "music.mp3");
    EXPECT_EQ(segs[1].start, 2 * kSec);
    EXPECT_EQ(segs[1].len, 2 * kSec);
    EXPECT_EQ(segs[1].srcStartPts, 96000 + 48000); // in-point + 1s into clip
}
