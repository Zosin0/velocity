// Export correctness gates (docs/10 §4): exact frame count, both streams
// present, duration within tolerance, decodable output. These are the tests
// that make "the export is broken" a CI failure instead of a support ticket.

#include <velocity/engine/edits.h>
#include <velocity/engine/export.h>
#include <velocity/media/probe.h>
#include <velocity/media/video_decoder.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

using namespace velocity;
using namespace velocity::engine;

namespace {
constexpr Tick kSec = kTickRate;

std::filesystem::path testMedia(const char* name) {
    const char* dir = std::getenv("VELOCITY_TESTMEDIA_DIR");
    EXPECT_NE(dir, nullptr);
    return std::filesystem::path(dir ? dir : ".") / name;
}

std::filesystem::path outPath(const char* name) {
    auto p = std::filesystem::temp_directory_path() / "velocity_export_tests";
    std::filesystem::create_directories(p);
    return p / name;
}

// Adds the counting_av fixture as linked video+audio clips.
SnapshotPtr addAvClip(SnapshotPtr seq, size_t videoTrack, size_t audioTrack, Tick dstStart,
                      Tick dstLen, Tick srcOffsetTicks = 0) {
    const auto asset = testMedia("counting_av.mp4");
    auto info = media::probe(asset);
    EXPECT_TRUE(info.hasValue());

    Clip v;
    v.asset = asset;
    v.dstStart = dstStart;
    v.dstLen = dstLen;
    v.srcTimebase = info->bestVideo->timebase;
    v.srcStartPts = ptsFromTicks(srcOffsetTicks, v.srcTimebase);
    auto s1 = addClip(seq, videoTrack, v);
    EXPECT_TRUE(s1.hasValue());

    Clip a;
    a.asset = asset;
    a.dstStart = dstStart;
    a.dstLen = dstLen;
    a.srcTimebase = info->bestAudio->timebase;
    a.srcStartPts = ptsFromTicks(srcOffsetTicks, a.srcTimebase);
    auto s2 = addClip(*s1, audioTrack, a);
    EXPECT_TRUE(s2.hasValue());
    return *s2;
}

std::int64_t countDecodedFrames(const std::filesystem::path& file) {
    auto dec = media::VideoDecoder::open(file);
    EXPECT_TRUE(dec.hasValue());
    std::int64_t n = 0;
    for (;;) {
        auto f = (*dec)->readNext();
        if (!f)
            break;
        ++n;
    }
    return n;
}
} // namespace

TEST(Export, ExpectedFrameCountMath) {
    EXPECT_EQ(expectedFrameCount(2 * kSec, {30, 1}), 60);
    EXPECT_EQ(expectedFrameCount(2 * kSec, {30000, 1001}), 60);
    EXPECT_EQ(expectedFrameCount(1, {30, 1}), 1);
    EXPECT_EQ(expectedFrameCount(0, {30, 1}), 0);
}

TEST(Export, SimpleTimelineMeetsGates) {
    auto seq = makeSequence(320, 240, {30, 1}, 1, 1);
    seq = addAvClip(seq, 0, 1, 0, 2 * kSec);

    const auto out = outPath("simple.mp4");
    ExportSettings settings;
    settings.videoBitrate = 2'000'000;
    auto result = exportSequence(seq, out, settings);
    ASSERT_TRUE(result.hasValue()) << result.error();

    // Gate 1: exact frame count.
    EXPECT_EQ(result->videoFrames, 60);
    EXPECT_EQ(countDecodedFrames(out), 60);
    // Gate 2: audio sample count == timeline duration (ticks == samples).
    EXPECT_EQ(result->audioSamples, 2 * kSec);

    // Gate 3: both streams present with expected properties.
    auto info = media::probe(out);
    ASSERT_TRUE(info.hasValue()) << info.error().message;
    ASSERT_TRUE(info->bestVideo.has_value());
    ASSERT_TRUE(info->bestAudio.has_value());
    EXPECT_EQ(info->bestVideo->codecName, "h264");
    EXPECT_EQ(info->bestAudio->codecName, "aac");
    EXPECT_EQ(info->bestVideo->width, 320);
    EXPECT_TRUE(info->bestVideo->frameRate == Rational(30, 1));
    // Gate 4: duration within 50 ms (AAC priming/last-frame padding allowed).
    EXPECT_NEAR(info->durationSeconds, 2.0, 0.05);
}

TEST(Export, CutAndGapTimelineExactFrames) {
    // Clip A: [0, 1s) from source start; gap; clip B: [1.5s, 2.5s) from 0.5s in.
    auto seq = makeSequence(320, 240, {30, 1}, 1, 1);
    seq = addAvClip(seq, 0, 1, 0, kSec, 0);
    seq = addAvClip(seq, 0, 1, kSec + kSec / 2, kSec, kSec / 2);

    const auto out = outPath("cutgap.mp4");
    ExportSettings settings;
    settings.videoBitrate = 2'000'000;
    auto result = exportSequence(seq, out, settings);
    ASSERT_TRUE(result.hasValue()) << result.error();

    // 2.5 s at 30 fps = 75 frames, including the black gap.
    EXPECT_EQ(result->videoFrames, 75);
    EXPECT_EQ(countDecodedFrames(out), 75);
    EXPECT_EQ(result->audioSamples, 2 * kSec + kSec / 2);

    auto info = media::probe(out);
    ASSERT_TRUE(info.hasValue());
    EXPECT_NEAR(info->durationSeconds, 2.5, 0.05);
}

TEST(Export, CancellationStopsCleanly) {
    auto seq = makeSequence(320, 240, {30, 1}, 1, 1);
    seq = addAvClip(seq, 0, 1, 0, 2 * kSec);

    const auto out = outPath("cancelled.mp4");
    int calls = 0;
    auto result = exportSequence(seq, out, {}, [&](double) { return ++calls < 2; });
    ASSERT_FALSE(result.hasValue());
    EXPECT_NE(result.error().find("cancel"), std::string::npos);
}

TEST(Export, EmptyTimelineFailsCleanly) {
    auto seq = makeSequence(320, 240, {30, 1}, 1, 1);
    auto result = exportSequence(seq, outPath("empty.mp4"));
    ASSERT_FALSE(result.hasValue());
}
