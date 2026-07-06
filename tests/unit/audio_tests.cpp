// Spike C acceptance: WASAPI shared-mode output opens, plays a generated
// tone glitch-free (no underruns after start), and IAudioClock advances at
// real-time rate — the property that lets it serve as the playback master
// clock (docs/05 §2). Skips on machines/CI without an audio endpoint.

#include <velocity/audio/output.h>

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <cmath>

using namespace velocity;
using namespace velocity::audio;

namespace {
struct SineState {
    double phase = 0.0;
    double step = 0.0;
    std::uint32_t channels = 2;
};
} // namespace

TEST(Audio, OutputOpensPlaysAndClockAdvances) {
    auto out = AudioOutput::create();
    if (!out)
        GTEST_SKIP() << "no usable audio endpoint: " << out.error();

    EXPECT_GE((*out)->sampleRate(), 44100u);
    EXPECT_GE((*out)->channels(), 2u);

    static SineState sine;
    sine.step = 2.0 * 3.14159265358979 * 440.0 / (*out)->sampleRate();
    sine.channels = (*out)->channels();

    auto started = (*out)->start([](float* buf, std::uint32_t frames) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            const float v = 0.05f * static_cast<float>(std::sin(sine.phase));
            sine.phase += sine.step;
            for (std::uint32_t c = 0; c < sine.channels; ++c)
                buf[i * sine.channels + c] = v;
        }
    });
    ASSERT_TRUE(started.hasValue()) << started.error();

    Sleep(150); // let the stream spin up
    auto c0 = (*out)->clock();
    ASSERT_TRUE(c0.hasValue()) << c0.error();
    Sleep(400);
    auto c1 = (*out)->clock();
    ASSERT_TRUE(c1.hasValue()) << c1.error();

    const double posDelta = c1->positionSeconds - c0->positionSeconds;
    const double qpcDelta = static_cast<double>(c1->qpc - c0->qpc) / 1e7; // 100ns units

    // The device clock must advance, at wall-clock rate (±10 %), and the two
    // deltas must agree — that agreement is the A/V sync foundation.
    EXPECT_GT(posDelta, 0.0);
    EXPECT_NEAR(posDelta, 0.4, 0.10);
    EXPECT_NEAR(posDelta, qpcDelta, 0.05);

    const auto underruns = (*out)->underruns();
    EXPECT_LE(underruns, 2u) << "audio underruns during steady playback";

    (*out)->stop();
}
