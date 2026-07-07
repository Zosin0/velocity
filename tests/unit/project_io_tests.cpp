// Project persistence round-trip (docs/03 canonical JSON): every model field
// survives save→load; malformed and future-versioned files fail cleanly.

#include <velocity/engine/edits.h>
#include <velocity/engine/project_io.h>

#include <gtest/gtest.h>

#include <filesystem>

using namespace velocity;
using namespace velocity::engine;

namespace {
constexpr Tick kSec = kTickRate;

SnapshotPtr richSequence() {
    auto seq = makeSequence(1280, 720, {30000, 1001}, 2, 1);
    Clip v;
    v.asset = L"C:/media/vídeo ünïcode.mp4"; // exercises UTF-8 path round-trip
    v.dstStart = kSec;
    v.dstLen = 2 * kSec;
    v.srcStartPts = 3003;
    v.srcTimebase = {1001, 30000};
    v.transform = {0.25f, -0.1f, 1.5f, 45.0f, 0.7f};
    v.hidden = false;
    auto s = addClip(seq, 0, v);

    Clip a;
    a.asset = "C:/media/music.mp3";
    a.dstStart = 0;
    a.dstLen = 3 * kSec;
    a.srcTimebase = {1, 44100};
    a.gain = 0.8f;
    a.mute = true;
    a.fadeIn = kSec / 2;
    a.fadeOut = kSec / 4;
    s = addClip(*s, 2, a);
    EXPECT_TRUE(s.hasValue());
    return *s;
}
} // namespace

TEST(ProjectIo, RoundTripPreservesEverything) {
    auto original = richSequence();
    auto restored = deserializeProject(serializeProject(*original));
    ASSERT_TRUE(restored.hasValue()) << restored.error();

    const Sequence& r = **restored;
    EXPECT_EQ(r.width, 1280);
    EXPECT_EQ(r.height, 720);
    EXPECT_TRUE(r.frameRate == Rational(30000, 1001));
    ASSERT_EQ(r.tracks.size(), 3u);
    EXPECT_EQ(r.tracks[0]->kind, TrackKind::video);
    EXPECT_EQ(r.tracks[2]->kind, TrackKind::audio);

    ASSERT_EQ(r.tracks[0]->clips.size(), 1u);
    const Clip& v = *r.tracks[0]->clips[0];
    EXPECT_EQ(v.asset, std::filesystem::path(L"C:/media/vídeo ünïcode.mp4"));
    EXPECT_EQ(v.dstStart, kSec);
    EXPECT_EQ(v.dstLen, 2 * kSec);
    EXPECT_EQ(v.srcStartPts, 3003);
    EXPECT_TRUE(v.srcTimebase == Rational(1001, 30000));
    EXPECT_FLOAT_EQ(v.transform.posX, 0.25f);
    EXPECT_FLOAT_EQ(v.transform.rotation, 45.0f);
    EXPECT_FLOAT_EQ(v.transform.opacity, 0.7f);

    ASSERT_EQ(r.tracks[2]->clips.size(), 1u);
    const Clip& a = *r.tracks[2]->clips[0];
    EXPECT_FLOAT_EQ(a.gain, 0.8f);
    EXPECT_TRUE(a.mute);
    EXPECT_EQ(a.fadeIn, kSec / 2);
    EXPECT_EQ(a.fadeOut, kSec / 4);
}

TEST(ProjectIo, SaveLoadFileRoundTrip) {
    const auto file = std::filesystem::temp_directory_path() / "velocity_test.velproj";
    auto original = richSequence();
    auto saved = saveProject(original, file);
    ASSERT_TRUE(saved.hasValue()) << saved.error();

    auto loaded = loadProject(file);
    ASSERT_TRUE(loaded.hasValue()) << loaded.error();
    EXPECT_EQ((*loaded)->duration(), original->duration());
    std::filesystem::remove(file);
}

TEST(ProjectIo, RejectsGarbageAndFutureVersions) {
    EXPECT_FALSE(deserializeProject("not json at all").hasValue());
    EXPECT_FALSE(deserializeProject("{}").hasValue());
    EXPECT_FALSE(deserializeProject(R"({"velocity_project": 999, "sequence": {}})").hasValue());
    EXPECT_FALSE(loadProject("Z:/does/not/exist.velproj").hasValue());
}
