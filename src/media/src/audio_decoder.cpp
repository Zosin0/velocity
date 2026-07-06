#include "velocity/media/audio_decoder.h"

#include "ffmpeg_util.h"

#include <velocity/foundation/time.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <cstring>
#include <vector>

namespace velocity::media {

namespace {
struct CodecContextDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct SwrDeleter {
    void operator()(SwrContext* c) const { swr_free(&c); }
};
} // namespace

struct AudioDecoder::Impl {
    FormatContextPtr fmt;
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codec;
    std::unique_ptr<SwrContext, SwrDeleter> swr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    int streamIdx = -1;
    int outRate = 48000;
    int outChannels = 2;
    std::int64_t lengthOut = 0;

    // Contiguous converted samples: buf covers [bufStart, bufStart+bufFrames)
    // in output-sample positions. Grown by decodeMore(), trimmed on seek.
    std::vector<float> buf;
    std::int64_t bufStart = 0;
    std::int64_t bufFrames() const { return static_cast<std::int64_t>(buf.size()) / outChannels; }
    bool eof = false;
    bool positionKnown = false;

    ~Impl() {
        av_packet_free(&pkt);
        av_frame_free(&frame);
    }

    Rational streamTb() const {
        AVStream* s = fmt->streams[streamIdx];
        return {s->time_base.num, s->time_base.den};
    }

    std::int64_t ptsToOutSamples(std::int64_t pts) const {
        const Rational tb = streamTb();
        // samples = pts * tb.num * outRate / tb.den (exact, floor)
        return detail::mulDiv(pts, tb.num * outRate, tb.den, false);
    }

    // Decodes the next audio frame and appends its converted samples.
    // Returns false at EOF.
    Expected<bool, MediaError> decodeMore() {
        if (eof)
            return false;
        for (;;) {
            const int rr = avcodec_receive_frame(codec.get(), frame);
            if (rr == 0)
                break;
            if (rr == AVERROR_EOF) {
                // Drain the resampler tail.
                flushSwrTail();
                eof = true;
                return false;
            }
            if (rr != AVERROR(EAGAIN))
                return makeUnexpected(
                    MediaError{MediaErrorKind::decode, "audio receive: " + avErrorText(rr)});
            for (;;) {
                const int pr = av_read_frame(fmt.get(), pkt);
                if (pr == AVERROR_EOF) {
                    avcodec_send_packet(codec.get(), nullptr);
                    break;
                }
                if (pr < 0)
                    return makeUnexpected(
                        MediaError{MediaErrorKind::io, "audio read: " + avErrorText(pr)});
                if (pkt->stream_index != streamIdx) {
                    av_packet_unref(pkt);
                    continue;
                }
                const int sr = avcodec_send_packet(codec.get(), pkt);
                av_packet_unref(pkt);
                if (sr < 0 && sr != AVERROR(EAGAIN))
                    return makeUnexpected(
                        MediaError{MediaErrorKind::decode, "audio send: " + avErrorText(sr)});
                break;
            }
        }

        // First decoded frame after open/seek anchors the buffer position.
        if (!positionKnown) {
            const std::int64_t pts = frame->best_effort_timestamp;
            bufStart = pts == AV_NOPTS_VALUE ? 0 : ptsToOutSamples(pts);
            positionKnown = true;
        }

        const int maxOut =
            static_cast<int>(av_rescale_rnd(swr_get_delay(swr.get(), codec->sample_rate) +
                                                frame->nb_samples,
                                            outRate, codec->sample_rate, AV_ROUND_UP));
        const size_t old = buf.size();
        buf.resize(old + static_cast<size_t>(maxOut) * outChannels);
        std::uint8_t* outPlane = reinterpret_cast<std::uint8_t*>(buf.data() + old);
        const int got = swr_convert(swr.get(), &outPlane, maxOut,
                                    const_cast<const std::uint8_t**>(frame->extended_data),
                                    frame->nb_samples);
        if (got < 0)
            return makeUnexpected(MediaError{MediaErrorKind::decode, "swr_convert failed"});
        buf.resize(old + static_cast<size_t>(got) * outChannels);
        return true;
    }

    void flushSwrTail() {
        const int maxOut = static_cast<int>(
            av_rescale_rnd(swr_get_delay(swr.get(), codec->sample_rate), outRate,
                           codec->sample_rate, AV_ROUND_UP));
        if (maxOut <= 0)
            return;
        const size_t old = buf.size();
        buf.resize(old + static_cast<size_t>(maxOut) * outChannels);
        std::uint8_t* outPlane = reinterpret_cast<std::uint8_t*>(buf.data() + old);
        const int got = swr_convert(swr.get(), &outPlane, maxOut, nullptr, 0);
        buf.resize(old + static_cast<size_t>(got > 0 ? got : 0) * outChannels);
    }

    Expected<void, MediaError> seekTo(std::int64_t pos) {
        const Rational tb = streamTb();
        // output samples → stream pts (floor)
        const std::int64_t pts = detail::mulDiv(pos, tb.den, tb.num * outRate, false);
        const int r = avformat_seek_file(fmt.get(), streamIdx, INT64_MIN, pts, pts, 0);
        if (r < 0)
            return makeUnexpected(MediaError{MediaErrorKind::io, "audio seek: " + avErrorText(r)});
        avcodec_flush_buffers(codec.get());
        swr_init(swr.get()); // reset resampler state
        buf.clear();
        bufStart = 0;
        positionKnown = false;
        eof = false;
        return {};
    }
};

AudioDecoder::~AudioDecoder() = default;
int AudioDecoder::rate() const { return impl_->outRate; }
int AudioDecoder::channels() const { return impl_->outChannels; }
std::int64_t AudioDecoder::lengthSamples() const { return impl_->lengthOut; }

Expected<std::unique_ptr<AudioDecoder>, MediaError>
AudioDecoder::open(const std::filesystem::path& file, int targetRate, int targetChannels) {
    using Ret = Expected<std::unique_ptr<AudioDecoder>, MediaError>;
    auto dec = std::unique_ptr<AudioDecoder>(new AudioDecoder());
    dec->impl_ = std::make_unique<Impl>();
    Impl& im = *dec->impl_;
    im.outRate = targetRate;
    im.outChannels = targetChannels;

    im.fmt = openInput(file);
    if (!im.fmt)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::io, "cannot open " + file.string()})};
    if (avformat_find_stream_info(im.fmt.get(), nullptr) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::unsupported, "no stream info"})};

    const AVCodec* codec = nullptr;
    im.streamIdx = av_find_best_stream(im.fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (im.streamIdx < 0 || !codec)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::unsupported, "no audio stream"})};

    AVStream* s = im.fmt->streams[im.streamIdx];
    im.codec.reset(avcodec_alloc_context3(codec));
    if (avcodec_parameters_to_context(im.codec.get(), s->codecpar) < 0 ||
        avcodec_open2(im.codec.get(), codec, nullptr) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::decode, "cannot open audio codec"})};

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, targetChannels);
    SwrContext* swr = nullptr;
    const int r = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, targetRate,
                                      &im.codec->ch_layout, im.codec->sample_fmt,
                                      im.codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outLayout);
    if (r < 0 || swr_init(swr) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::unsupported, "resampler init failed"})};
    im.swr.reset(swr);

    im.pkt = av_packet_alloc();
    im.frame = av_frame_alloc();
    if (s->duration > 0)
        im.lengthOut = im.ptsToOutSamples(s->duration);

    return Ret{std::move(dec)};
}

Expected<void, MediaError> AudioDecoder::readAt(std::int64_t pos, float* out, int frames) {
    Impl& im = *impl_;
    std::memset(out, 0, static_cast<size_t>(frames) * im.outChannels * sizeof(float));
    if (frames <= 0)
        return {};

    // Need a buffer window covering pos; reseek when the request is before
    // the window or far ahead of it (> 1 s gap).
    if (!im.positionKnown || pos < im.bufStart ||
        pos > im.bufStart + im.bufFrames() + im.outRate) {
        if (auto s = im.seekTo(pos); !s)
            return s;
    }

    for (;;) {
        // Drop already-consumed samples to bound memory (keep the window tight).
        const std::int64_t excess = pos - im.bufStart;
        if (excess > im.outRate) {
            const std::int64_t drop = excess - 1024;
            if (drop > 0 && drop <= im.bufFrames()) {
                im.buf.erase(im.buf.begin(),
                             im.buf.begin() + static_cast<size_t>(drop) * im.outChannels);
                im.bufStart += drop;
            }
        }
        if (im.bufStart + im.bufFrames() >= pos + frames || im.eof)
            break;
        auto more = im.decodeMore();
        if (!more)
            return makeUnexpected(more.error());
    }

    // Copy the overlap of [pos, pos+frames) with the buffered window.
    const std::int64_t copyFrom = std::max(pos, im.bufStart);
    const std::int64_t copyTo = std::min<std::int64_t>(pos + frames, im.bufStart + im.bufFrames());
    if (copyFrom < copyTo) {
        const std::int64_t srcOff = (copyFrom - im.bufStart) * im.outChannels;
        const std::int64_t dstOff = (copyFrom - pos) * im.outChannels;
        std::memcpy(out + dstOff, im.buf.data() + srcOff,
                    static_cast<size_t>(copyTo - copyFrom) * im.outChannels * sizeof(float));
    }
    return {};
}

} // namespace velocity::media
