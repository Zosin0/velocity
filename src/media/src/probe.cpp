#include "velocity/media/probe.h"

#include "ffmpeg_util.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace velocity::media {

Expected<MediaInfo, MediaError> probe(const std::filesystem::path& file) {
    FormatContextPtr fmt = openInput(file);
    if (!fmt)
        return makeUnexpected(MediaError{MediaErrorKind::io, "cannot open " + file.string()});

    if (avformat_find_stream_info(fmt.get(), nullptr) < 0)
        return makeUnexpected(
            MediaError{MediaErrorKind::unsupported, "no stream info in " + file.string()});

    MediaInfo info;
    info.containerName = fmt->iformat ? fmt->iformat->name : "";
    if (fmt->duration > 0)
        info.durationSeconds = static_cast<double>(fmt->duration) / AV_TIME_BASE;

    const int vIdx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx >= 0) {
        AVStream* s = fmt->streams[vIdx];
        VideoStreamInfo v;
        v.index = vIdx;
        v.width = s->codecpar->width;
        v.height = s->codecpar->height;
        const AVRational fr = av_guess_frame_rate(fmt.get(), s, nullptr);
        v.frameRate = {fr.num, fr.den == 0 ? 1 : fr.den};
        v.timebase = {s->time_base.num, s->time_base.den};
        v.durationPts = s->duration > 0 ? s->duration : 0;
        v.codecName = avcodec_get_name(s->codecpar->codec_id);
        info.bestVideo = std::move(v);
    }

    const int aIdx = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (aIdx >= 0) {
        AVStream* s = fmt->streams[aIdx];
        AudioStreamInfo a;
        a.index = aIdx;
        a.sampleRate = s->codecpar->sample_rate;
        a.channels = s->codecpar->ch_layout.nb_channels;
        a.timebase = {s->time_base.num, s->time_base.den};
        a.durationPts = s->duration > 0 ? s->duration : 0;
        a.codecName = avcodec_get_name(s->codecpar->codec_id);
        info.bestAudio = std::move(a);
    }

    if (!info.bestVideo && !info.bestAudio)
        return makeUnexpected(
            MediaError{MediaErrorKind::unsupported, "no usable streams in " + file.string()});

    return info;
}

} // namespace velocity::media
