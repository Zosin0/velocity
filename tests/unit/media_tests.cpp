// Phase-0 FFmpeg integration smoke tests: headers, import libs, and runtime
// DLLs must be one coherent set, and the build must be the LGPL variant
// (licensing stance, docs/01 §4).

#include <velocity/media/ffmpeg_info.h>

#include <gtest/gtest.h>

TEST(FFmpegIntegration, RuntimeMatchesHeaders) {
    EXPECT_TRUE(velocity::media::runtimeMatchesHeaders());
}

TEST(FFmpegIntegration, RuntimeIsLgpl) {
    const auto v = velocity::media::runtimeVersions();
    EXPECT_NE(v.license.find("LGPL"), std::string::npos)
        << "shipped FFmpeg must be the LGPL build, got: " << v.license;
}
