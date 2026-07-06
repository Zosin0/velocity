#include "velocity/media/video_decoder.h"

#include "ffmpeg_util.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include <optional>

namespace velocity::media {

namespace {

struct CodecContextDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

// Offered-format callback used when a D3D11VA device is attached: prefer the
// hardware surface format, fall back to the first software format offered.
AVPixelFormat chooseHwFormat([[maybe_unused]] AVCodecContext* ctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_D3D11)
            return *p;
    return fmts[0];
}

} // namespace

// ---------------------------------------------------------------- VideoFrame

struct VideoFrame::Impl {
    AVFrame* frame = nullptr;
    Rational tb{0, 1};
    ~Impl() { av_frame_free(&frame); }
};

int VideoFrame::width() const { return impl_->frame->width; }
int VideoFrame::height() const { return impl_->frame->height; }
std::int64_t VideoFrame::pts() const { return impl_->frame->best_effort_timestamp; }
std::int64_t VideoFrame::duration() const { return impl_->frame->duration; }
Rational VideoFrame::timebase() const { return impl_->tb; }
bool VideoFrame::isHardware() const { return impl_->frame->format == AV_PIX_FMT_D3D11; }
const std::uint8_t* VideoFrame::data(int plane) const { return impl_->frame->data[plane]; }
int VideoFrame::stride(int plane) const { return impl_->frame->linesize[plane]; }

Expected<VideoFrame, MediaError> VideoFrame::transferToCpu() const {
    if (!isHardware())
        return *this;
    auto impl = std::make_shared<Impl>();
    impl->tb = impl_->tb;
    impl->frame = av_frame_alloc();
    if (const int r = av_hwframe_transfer_data(impl->frame, impl_->frame, 0); r < 0)
        return makeUnexpected(
            MediaError{MediaErrorKind::decode, "hw frame transfer failed: " + avErrorText(r)});
    av_frame_copy_props(impl->frame, impl_->frame);
    return VideoFrame{std::move(impl)};
}

// -------------------------------------------------------------- VideoDecoder

struct VideoDecoder::Impl {
    FormatContextPtr fmt;
    CodecContextPtr codec;
    PacketPtr pkt;
    AVBufferRef* hwDevice = nullptr;
    int streamIdx = -1;
    bool draining = false;
    VideoStreamInfo info;

    ~Impl() { av_buffer_unref(&hwDevice); }

    Expected<VideoFrame, MediaError> receiveOrFeed() {
        for (;;) {
            auto frameImpl = std::make_shared<VideoFrame::Impl>();
            frameImpl->frame = av_frame_alloc();
            frameImpl->tb = info.timebase;

            const int rr = avcodec_receive_frame(codec.get(), frameImpl->frame);
            if (rr == 0)
                return VideoFrame{std::move(frameImpl)};
            if (rr == AVERROR_EOF)
                return makeUnexpected(MediaError{MediaErrorKind::endOfStream, "end of stream"});
            if (rr != AVERROR(EAGAIN))
                return makeUnexpected(
                    MediaError{MediaErrorKind::decode, "receive_frame: " + avErrorText(rr)});

            // Decoder wants input.
            for (;;) {
                const int pr = av_read_frame(fmt.get(), pkt.get());
                if (pr == AVERROR_EOF) {
                    avcodec_send_packet(codec.get(), nullptr); // begin drain
                    draining = true;
                    break;
                }
                if (pr < 0)
                    return makeUnexpected(
                        MediaError{MediaErrorKind::io, "read_frame: " + avErrorText(pr)});
                if (pkt->stream_index != streamIdx) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                const int sr = avcodec_send_packet(codec.get(), pkt.get());
                av_packet_unref(pkt.get());
                if (sr < 0 && sr != AVERROR(EAGAIN))
                    return makeUnexpected(
                        MediaError{MediaErrorKind::decode, "send_packet: " + avErrorText(sr)});
                break;
            }
        }
    }
};

VideoDecoder::VideoDecoder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
VideoDecoder::~VideoDecoder() = default;
const VideoStreamInfo& VideoDecoder::stream() const { return impl_->info; }
bool VideoDecoder::usingHardware() const { return impl_->hwDevice != nullptr; }

Expected<std::unique_ptr<VideoDecoder>, MediaError>
VideoDecoder::open(const std::filesystem::path& file, const DecodeOptions& opts) {
    using Ret = Expected<std::unique_ptr<VideoDecoder>, MediaError>;

    auto impl = std::make_unique<Impl>();
    impl->fmt = openInput(file);
    if (!impl->fmt)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::io, "cannot open " + file.string()})};
    if (avformat_find_stream_info(impl->fmt.get(), nullptr) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::unsupported, "no stream info"})};

    const AVCodec* dec = nullptr;
    impl->streamIdx =
        av_find_best_stream(impl->fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (impl->streamIdx < 0 || !dec)
        return Ret{makeUnexpected(
            MediaError{MediaErrorKind::unsupported, "no decodable video stream"})};

    AVStream* s = impl->fmt->streams[impl->streamIdx];
    impl->codec.reset(avcodec_alloc_context3(dec));
    if (avcodec_parameters_to_context(impl->codec.get(), s->codecpar) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::decode, "codec params"})};

    if (opts.preferHardware) {
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(dec, i);
            if (!cfg)
                break;
            if (cfg->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
                (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                if (av_hwdevice_ctx_create(&impl->hwDevice, AV_HWDEVICE_TYPE_D3D11VA, nullptr,
                                           nullptr, 0) == 0) {
                    impl->codec->hw_device_ctx = av_buffer_ref(impl->hwDevice);
                    impl->codec->get_format = chooseHwFormat;
                }
                break;
            }
        }
    }

    if (avcodec_open2(impl->codec.get(), dec, nullptr) < 0)
        return Ret{makeUnexpected(MediaError{MediaErrorKind::decode, "cannot open decoder"})};

    impl->pkt.reset(av_packet_alloc());

    VideoStreamInfo& info = impl->info;
    info.index = impl->streamIdx;
    info.width = s->codecpar->width;
    info.height = s->codecpar->height;
    const AVRational fr = av_guess_frame_rate(impl->fmt.get(), s, nullptr);
    info.frameRate = {fr.num, fr.den == 0 ? 1 : fr.den};
    info.timebase = {s->time_base.num, s->time_base.den};
    info.durationPts = s->duration > 0 ? s->duration : 0;
    info.codecName = avcodec_get_name(s->codecpar->codec_id);

    return Ret{std::unique_ptr<VideoDecoder>{new VideoDecoder(std::move(impl))}};
}

Expected<VideoFrame, MediaError> VideoDecoder::readNext() { return impl_->receiveOrFeed(); }

Expected<VideoFrame, MediaError> VideoDecoder::readFrameAt(std::int64_t targetPts) {
    // Correctness-first for the spike: always seek to the previous keyframe
    // and roll forward. Sequential-locality reuse arrives with DecodeService.
    const int r = avformat_seek_file(impl_->fmt.get(), impl_->streamIdx, INT64_MIN, targetPts,
                                     targetPts, 0);
    if (r < 0)
        return makeUnexpected(MediaError{MediaErrorKind::io, "seek: " + avErrorText(r)});
    avcodec_flush_buffers(impl_->codec.get());
    impl_->draining = false;

    std::optional<VideoFrame> prev;
    for (;;) {
        auto f = readNext();
        if (!f) {
            if (f.error().isEndOfStream() && prev)
                return *prev; // target beyond last frame: clamp to last
            return f;
        }
        if (f->pts() > targetPts)
            return prev ? *prev : *f; // passed it: previous frame contains target
        if (f->duration() > 0 && f->pts() + f->duration() > targetPts)
            return *f;
        prev = *f;
    }
}

} // namespace velocity::media
