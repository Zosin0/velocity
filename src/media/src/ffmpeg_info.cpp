#include "velocity/media/ffmpeg_info.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace velocity::media {

FFmpegVersions runtimeVersions() {
    FFmpegVersions v;
    v.avutil = avutil_version();
    v.avcodec = avcodec_version();
    v.avformat = avformat_version();
    v.license = avformat_license();
    return v;
}

bool runtimeMatchesHeaders() {
    const FFmpegVersions v = runtimeVersions();
    return AV_VERSION_MAJOR(v.avutil) == LIBAVUTIL_VERSION_MAJOR &&
           AV_VERSION_MAJOR(v.avcodec) == LIBAVCODEC_VERSION_MAJOR &&
           AV_VERSION_MAJOR(v.avformat) == LIBAVFORMAT_VERSION_MAJOR;
}

} // namespace velocity::media
