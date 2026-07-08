// Spike A acceptance tests (docs/PROGRESS.md): probing reports correct
// stream facts, sequential decode yields the exact expected frame count with
// monotonic pts, and random access (readFrameAt) returns bit-identical pts to
// a sequential scan — the frame-accuracy guarantee the whole editor sits on.

#include <velocity/media/probe.h>
#include <velocity/media/sequential_reader.h>
#include <velocity/media/video_decoder.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace velocity;
using namespace velocity::media;

namespace {

std::filesystem::path testMedia(const char* name) {
    const char* dir = std::getenv("VELOCITY_TESTMEDIA_DIR");
    EXPECT_NE(dir, nullptr) << "VELOCITY_TESTMEDIA_DIR not set";
    return std::filesystem::path(dir ? dir : ".") / name;
}

std::vector<std::int64_t> sequentialPtsScan(const std::filesystem::path& file,
                                            const DecodeOptions& opts = {}) {
    auto dec = VideoDecoder::open(file, opts);
    EXPECT_TRUE(dec.hasValue());
    std::vector<std::int64_t> pts;
    for (;;) {
        auto f = (*dec)->readNext();
        if (!f) {
            EXPECT_TRUE(f.error().isEndOfStream()) << f.error().message;
            break;
        }
        pts.push_back(f->pts());
    }
    return pts;
}

} // namespace

TEST(Probe, Reports30FpsFixtureCorrectly) {
    auto info = probe(testMedia("counting_30fps.mp4"));
    ASSERT_TRUE(info.hasValue()) << info.error().message;
    ASSERT_TRUE(info->bestVideo.has_value());
    EXPECT_EQ(info->bestVideo->width, 320);
    EXPECT_EQ(info->bestVideo->height, 240);
    EXPECT_EQ(info->bestVideo->codecName, "h264");
    EXPECT_TRUE(info->bestVideo->frameRate == Rational(30, 1));
    EXPECT_NEAR(info->durationSeconds, 2.0, 0.05);
}

TEST(Probe, ReportsNtscRateExactly) {
    auto info = probe(testMedia("counting_ntsc.mp4"));
    ASSERT_TRUE(info.hasValue()) << info.error().message;
    ASSERT_TRUE(info->bestVideo.has_value());
    EXPECT_TRUE(info->bestVideo->frameRate == Rational(30000, 1001))
        << info->bestVideo->frameRate.num << "/" << info->bestVideo->frameRate.den;
}

TEST(Probe, FailsCleanlyOnMissingFile) {
    auto info = probe(testMedia("does_not_exist.mp4"));
    ASSERT_FALSE(info.hasValue());
    EXPECT_EQ(info.error().kind, MediaErrorKind::io);
}

TEST(Decoder, SequentialDecodeExactFrameCount) {
    const auto pts30 = sequentialPtsScan(testMedia("counting_30fps.mp4"));
    EXPECT_EQ(pts30.size(), 60u);
    const auto ptsNtsc = sequentialPtsScan(testMedia("counting_ntsc.mp4"));
    EXPECT_EQ(ptsNtsc.size(), 120u);
}

TEST(Decoder, PtsMonotonicFromZero) {
    const auto pts = sequentialPtsScan(testMedia("counting_30fps.mp4"));
    ASSERT_FALSE(pts.empty());
    EXPECT_EQ(pts.front(), 0);
    for (size_t i = 1; i < pts.size(); ++i)
        EXPECT_GT(pts[i], pts[i - 1]) << "at frame " << i;
}

TEST(Decoder, RandomAccessMatchesSequentialScan) {
    const auto file = testMedia("counting_ntsc.mp4");
    const auto pts = sequentialPtsScan(file);
    ASSERT_EQ(pts.size(), 120u);

    auto dec = VideoDecoder::open(file);
    ASSERT_TRUE(dec.hasValue());
    // Frame indices chosen to hit: start, mid-GOP, keyframes, last frame.
    for (const size_t idx : {0u, 1u, 7u, 29u, 30u, 59u, 60u, 118u, 119u}) {
        auto f = (*dec)->readFrameAt(pts[idx]);
        ASSERT_TRUE(f.hasValue()) << "frame " << idx << ": " << f.error().message;
        EXPECT_EQ(f->pts(), pts[idx]) << "frame " << idx;
    }
    // A pts in the middle of frame N's display interval still resolves to N.
    auto mid = (*dec)->readFrameAt(pts[50] + (pts[51] - pts[50]) / 2);
    ASSERT_TRUE(mid.hasValue());
    EXPECT_EQ(mid->pts(), pts[50]);
    // Beyond the end clamps to the last frame.
    auto last = (*dec)->readFrameAt(pts.back() * 10);
    ASSERT_TRUE(last.hasValue());
    EXPECT_EQ(last->pts(), pts.back());
}

TEST(Decoder, CpuFrameHasReadablePlanes) {
    auto dec = VideoDecoder::open(testMedia("counting_30fps.mp4"));
    ASSERT_TRUE(dec.hasValue());
    auto f = (*dec)->readNext();
    ASSERT_TRUE(f.hasValue());
    EXPECT_FALSE(f->isHardware());
    EXPECT_NE(f->data(0), nullptr); // Y
    EXPECT_NE(f->data(1), nullptr); // U
    EXPECT_NE(f->data(2), nullptr); // V
    EXPECT_GE(f->stride(0), 320);
}

// Spike A hardware guarantee: when D3D11VA is available, the hardware path
// produces the same pts sequence as software, and surfaces transfer to CPU.
// Skipped (not failed) on machines/CI without a D3D11VA-capable device.
TEST(Decoder, HardwareDecodeMatchesSoftware) {
    const auto file = testMedia("counting_30fps.mp4");
    DecodeOptions hw;
    hw.preferHardware = true;
    auto dec = VideoDecoder::open(file, hw);
    ASSERT_TRUE(dec.hasValue());
    if (!(*dec)->usingHardware())
        GTEST_SKIP() << "no D3D11VA device available";

    auto first = (*dec)->readNext();
    ASSERT_TRUE(first.hasValue());
    if (first->isHardware()) {
        auto cpu = first->transferToCpu();
        ASSERT_TRUE(cpu.hasValue()) << cpu.error().message;
        EXPECT_FALSE(cpu->isHardware());
        EXPECT_NE(cpu->data(0), nullptr);
        EXPECT_EQ(cpu->width(), 320);
    }

    const auto swPts = sequentialPtsScan(file);
    auto hwPts = sequentialPtsScan(file, hw);
    EXPECT_EQ(swPts, hwPts);
}

// Sub-frame requests must serve the cached frame, not decode ahead: the UI
// updates the playhead at ~60 Hz over 30 fps media, so a pts inside the
// last frame's display interval has to return that same frame (regression:
// it used to decode and return the NEXT frame, one frame early, every tick).
TEST(SequentialReader, SubFrameRequestsReuseTheCachedFrame) {
    auto dec = VideoDecoder::open(testMedia("counting_30fps.mp4"));
    ASSERT_TRUE(dec.hasValue());
    SequentialFrameReader reader(std::move(dec.value()));

    auto f0 = reader.at(0);
    ASSERT_TRUE(f0.hasValue()) << f0.error().message;
    ASSERT_GT(f0->duration(), 0);
    const std::int64_t pts0 = f0->pts();
    const std::int64_t dur = f0->duration();

    // Anywhere inside frame 0's interval → frame 0 again.
    EXPECT_EQ(reader.at(pts0 + dur / 2)->pts(), pts0);
    EXPECT_EQ(reader.at(pts0 + dur - 1)->pts(), pts0);
    // First tick of the next interval → frame 1 exactly.
    EXPECT_EQ(reader.at(pts0 + dur)->pts(), pts0 + dur);
    // And moving backwards inside frame 1 must not rewind to frame 0.
    EXPECT_EQ(reader.at(pts0 + dur + 1)->pts(), pts0 + dur);
}
