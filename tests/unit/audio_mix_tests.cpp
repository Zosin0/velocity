// AudioMixer behavior (docs/07 §3): gain, mute, linear fades, gap silence.
// Uses the 440 Hz tone fixture; assertions are on RMS energy so they are
// robust to codec ringing.

#include <velocity/engine/audio_mix.h>
#include <velocity/engine/edits.h>
#include <velocity/media/probe.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

using namespace velocity;
using namespace velocity::engine;

namespace {
constexpr Tick kSec = kTickRate;

std::filesystem::path avFixture() {
    const char* dir = std::getenv("VELOCITY_TESTMEDIA_DIR");
    EXPECT_NE(dir, nullptr);
    return std::filesystem::path(dir ? dir : ".") / "counting_av.mp4";
}

SnapshotPtr toneTimeline(float gain = 1.0f, Tick fadeIn = 0, Tick fadeOut = 0,
                         bool mute = false) {
    auto seq = makeSequence(320, 240, {30, 1}, 0, 1);
    auto info = media::probe(avFixture());
    EXPECT_TRUE(info.hasValue());
    Clip c;
    c.asset = avFixture();
    c.dstStart = 0;
    c.dstLen = 2 * kSec;
    c.srcTimebase = info->bestAudio->timebase;
    c.gain = gain;
    c.fadeIn = fadeIn;
    c.fadeOut = fadeOut;
    c.mute = mute;
    auto s = addClip(seq, 0, c);
    EXPECT_TRUE(s.hasValue());
    return *s;
}

double rmsOf(AudioMixer& mixer, const Sequence& seq, Tick pos, int frames,
             float master = 1.0f) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    mixer.mix(seq, pos, frames, buf.data(), master);
    double acc = 0;
    for (float s : buf)
        acc += static_cast<double>(s) * s;
    return std::sqrt(acc / static_cast<double>(buf.size()));
}
} // namespace

TEST(AudioMix, ToneProducesEnergyAndGapsAreSilent) {
    auto seq = toneTimeline();
    AudioMixer mixer;
    // Mid-clip: clear signal (ffmpeg's sine source is ~1/8 amplitude,
    // RMS ≈ 0.06; anything above 0.03 means real audio came through).
    EXPECT_GT(rmsOf(mixer, *seq, kSec / 2, 4800), 0.03);
    // Past the clip: exact silence.
    EXPECT_EQ(rmsOf(mixer, *seq, 3 * kSec, 4800), 0.0);
}

TEST(AudioMix, GainScalesLinearly) {
    auto loud = toneTimeline(1.0f);
    auto quiet = toneTimeline(0.25f);
    AudioMixer m1, m2;
    const double full = rmsOf(m1, *loud, kSec / 2, 4800);
    const double quarter = rmsOf(m2, *quiet, kSec / 2, 4800);
    EXPECT_NEAR(quarter / full, 0.25, 0.02);
}

TEST(AudioMix, MuteIsSilent) {
    auto seq = toneTimeline(1.0f, 0, 0, /*mute=*/true);
    AudioMixer mixer;
    EXPECT_EQ(rmsOf(mixer, *seq, kSec / 2, 4800), 0.0);
}

TEST(AudioMix, FadeInRampsUp) {
    auto seq = toneTimeline(1.0f, /*fadeIn=*/kSec, 0);
    AudioMixer mixer;
    const double early = rmsOf(mixer, *seq, kSec / 10, 2400);  // ~10% in
    const double late = rmsOf(mixer, *seq, kSec + kSec / 2, 2400); // after fade
    EXPECT_LT(early, late * 0.3);
    EXPECT_GT(early, 0.0); // ramp, not gate
}

TEST(AudioMix, MasterGainApplies) {
    auto seq = toneTimeline();
    AudioMixer m1, m2;
    const double full = rmsOf(m1, *seq, kSec / 2, 4800, 1.0f);
    const double half = rmsOf(m2, *seq, kSec / 2, 4800, 0.5f);
    EXPECT_NEAR(half / full, 0.5, 0.03);
}

TEST(Edits, UpdateClipMutatesPropertiesImmutably) {
    auto seq = toneTimeline();
    const ClipId id = seq->tracks[0]->clips[0]->id;

    auto updated = updateClip(seq, 0, id, [](Clip& c) {
        c.gain = 0.5f;
        c.fadeOut = kSec / 4;
        c.transform.opacity = 0.8f;
    });
    ASSERT_TRUE(updated.hasValue()) << updated.error();
    EXPECT_EQ(seq->tracks[0]->clips[0]->gain, 1.0f); // original untouched
    EXPECT_EQ((*updated)->tracks[0]->clips[0]->gain, 0.5f);
    EXPECT_EQ((*updated)->tracks[0]->clips[0]->fadeOut, kSec / 4);
    EXPECT_EQ((*updated)->tracks[0]->clips[0]->id, id);
}

TEST(Edits, MoveClipToTrackRespectsKindAndOverlap) {
    auto seq = makeSequence(320, 240, {30, 1}, 2, 1);
    Clip c;
    c.asset = "a.mp4";
    c.dstStart = 0;
    c.dstLen = kSec;
    auto s = addClip(seq, 0, c);
    ASSERT_TRUE(s.hasValue());
    const ClipId id = (*s)->tracks[0]->clips[0]->id;

    // Video → video track: ok, position change applied.
    auto moved = moveClipToTrack(*s, 0, id, 1, kSec);
    ASSERT_TRUE(moved.hasValue()) << moved.error();
    EXPECT_TRUE((*moved)->tracks[0]->clips.empty());
    ASSERT_EQ((*moved)->tracks[1]->clips.size(), 1u);
    EXPECT_EQ((*moved)->tracks[1]->clips[0]->dstStart, kSec);

    // Video → audio track: rejected.
    EXPECT_FALSE(moveClipToTrack(*moved, 1, id, 2, 0).hasValue());
}
