// Velocity entry point. Phase 0: initialize logging, verify the FFmpeg
// runtime, exit cleanly. The window/engine bring-up replaces this in Phase 1.

#include <velocity/foundation/log.h>
#include <velocity/media/ffmpeg_info.h>

int main() {
    velocity::log::init("velocity");
    spdlog::info("Velocity 0.1.0 starting");

    const auto v = velocity::media::runtimeVersions();
    spdlog::info("FFmpeg runtime: avutil {}.{} avcodec {}.{} avformat {}.{} ({})",
                 v.avutil >> 16, (v.avutil >> 8) & 0xff, v.avcodec >> 16, (v.avcodec >> 8) & 0xff,
                 v.avformat >> 16, (v.avformat >> 8) & 0xff, v.license);

    if (!velocity::media::runtimeMatchesHeaders()) {
        spdlog::error("FFmpeg runtime/header version mismatch");
        return 1;
    }

    spdlog::info("clean exit");
    return 0;
}
