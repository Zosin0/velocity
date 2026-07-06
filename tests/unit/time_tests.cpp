// Rational time math — the frame-accuracy foundation (docs/02 §4, docs/13).
// The "ugly fps" set (23.976/29.97/59.94) is exercised exactly, per the test
// strategy's requirement that NTSC rates never accumulate drift.

#include <velocity/foundation/time.h>

#include <gtest/gtest.h>

using namespace velocity;

namespace {
const Rational kNtsc30{30000, 1001};
const Rational kNtsc24{24000, 1001};
const Rational kNtsc60{60000, 1001};
const Rational kPal25{25, 1};
const Rational kExact30{30, 1};
} // namespace

TEST(RationalTime, NormalizeAndCompare) {
    EXPECT_EQ(Rational(2, 4).normalized().num, 1);
    EXPECT_EQ(Rational(2, 4).normalized().den, 2);
    EXPECT_TRUE(Rational(1, 3) < Rational(1, 2));
    EXPECT_TRUE(Rational(30000, 1001) == Rational(60000, 2002));
    // Values whose cross-products overflow 64-bit still compare correctly.
    EXPECT_TRUE(Rational(INT64_MAX / 2, INT64_MAX / 3) < Rational(INT64_MAX / 2, INT64_MAX / 4));
}

TEST(RationalTime, FrameTickRoundTripExactFps) {
    // At exact rates every frame boundary is an integer tick.
    for (std::int64_t f = 0; f < 100000; f += 7) {
        const Tick t = ticksFromFrameIndex(f, kExact30);
        EXPECT_EQ(frameIndexFromTicks(t, kExact30), f);
        EXPECT_EQ(t, f * kTickRate / 30);
    }
}

TEST(RationalTime, FrameTickRoundTripNtsc) {
    // NTSC frame boundaries are not integer ticks; the contract is:
    // frameIndexFromTicks(ticksFromFrameIndex(f)) == f for all f (no drift),
    // including far into a multi-hour timeline.
    for (const auto& fps : {kNtsc24, kNtsc30, kNtsc60}) {
        for (std::int64_t f = 0; f < 1'000'000; f += 997) {
            const Tick t = ticksFromFrameIndex(f, fps);
            EXPECT_EQ(frameIndexFromTicks(t, fps), f) << "fps=" << fps.num << "/" << fps.den
                                                      << " frame=" << f;
            // The tick just before this frame's start belongs to the previous frame.
            if (f > 0)
                EXPECT_EQ(frameIndexFromTicks(t - 1, fps), f - 1);
        }
    }
}

TEST(RationalTime, TenHourNtscTimelineFrameCount) {
    // 10 hours at 29.97: expected frame count is floor(36000s * 30000/1001).
    const Tick tenHours = 36000 * kTickRate;
    EXPECT_EQ(frameIndexFromTicks(tenHours, kNtsc30), 36000LL * 30000 / 1001);
}

TEST(RationalTime, PtsConversionCommonTimebases) {
    // 90 kHz MPEG timebase: pts 90000 == 1 second == kTickRate ticks.
    EXPECT_EQ(ticksFromPts(90000, {1, 90000}), kTickRate);
    // Typical mp4 video track timebase 1/30000: one frame at 29.97 is 1001 units.
    EXPECT_EQ(ticksFromPts(1001, {1, 30000}), kTickRate * 1001 / 30000);
    // Round trip through a hostile timebase keeps ordering (floor semantics).
    const Rational tb{1001, 48000};
    for (std::int64_t pts = 0; pts < 5000; ++pts) {
        const Tick t = ticksFromPts(pts, tb);
        EXPECT_LE(ptsFromTicks(t, tb), pts);
        EXPECT_GT(ticksFromPts(pts + 1, tb), t - 1);
    }
}

TEST(RationalTime, AudioSampleAlignment) {
    // Design invariant from docs/07: ticks map 1:1 to 48 kHz samples.
    EXPECT_EQ(kTickRate, 48000);
    EXPECT_EQ(ticksFromPts(48000, {1, 48000}), 48000);
}

TEST(RationalTime, PalIsTrivial) {
    for (std::int64_t f = 0; f < 10000; ++f)
        EXPECT_EQ(ticksFromFrameIndex(f, kPal25), f * 1920);
}
