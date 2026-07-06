#include "velocity/media/mp4_writer.h"

#include "ffmpeg_util.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstring>
#include <vector>

namespace velocity::media {

namespace {
struct CodecContextDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

MediaError mediaErr(MediaErrorKind kind, const char* what, int averr = 0) {
    std::string msg = what;
    if (averr != 0)
        msg += ": " + avErrorText(averr);
    return MediaError{kind, std::move(msg)};
}
} // namespace

struct Mp4Writer::Impl {
    AVFormatContext* oc = nullptr;
    CodecContextPtr venc;
    CodecContextPtr aenc;
    AVStream* vstream = nullptr;
    AVStream* astream = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* encFrame = nullptr;   // yuv420p output-size staging frame
    AVFrame* audioFrame = nullptr; // fltp staging frame
    SwsContext* sws = nullptr;
    ExportFormat fmt;
    std::string encoderName;
    std::int64_t videoPts = 0;      // frames
    std::int64_t audioPts = 0;      // samples pushed to the encoder
    std::int64_t audioAccepted = 0; // samples handed to writeAudio
    std::vector<float> pendingAudio;
    bool finished = false;

    ~Impl() {
        av_packet_free(&pkt);
        av_frame_free(&encFrame);
        av_frame_free(&audioFrame);
        sws_freeContext(sws);
        if (oc) {
            if (oc->pb)
                avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    }

    Expected<void, MediaError> drain(AVCodecContext* enc, AVStream* stream) {
        for (;;) {
            const int r = avcodec_receive_packet(enc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                return {};
            if (r < 0)
                return makeUnexpected(mediaErr(MediaErrorKind::decode, "receive_packet", r));
            av_packet_rescale_ts(pkt, enc->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            const int w = av_interleaved_write_frame(oc, pkt);
            av_packet_unref(pkt);
            if (w < 0)
                return makeUnexpected(mediaErr(MediaErrorKind::io, "write_frame", w));
        }
    }

    Expected<void, MediaError> encodeVideo(AVFrame* f) {
        const int r = avcodec_send_frame(venc.get(), f);
        if (r < 0)
            return makeUnexpected(mediaErr(MediaErrorKind::decode, "video send_frame", r));
        return drain(venc.get(), vstream);
    }

    Expected<void, MediaError> flushFullAudioFrames(bool padFinal) {
        const int frameSize = aenc->frame_size > 0 ? aenc->frame_size : 1024;
        const int ch = fmt.audioChannels;
        for (;;) {
            const int have = static_cast<int>(pendingAudio.size()) / ch;
            if (have < frameSize && !(padFinal && have > 0))
                return {};
            const int n = have >= frameSize ? frameSize : have;

            if (av_frame_make_writable(audioFrame) < 0)
                return makeUnexpected(mediaErr(MediaErrorKind::decode, "audio frame writable"));
            audioFrame->nb_samples = frameSize;
            // Deinterleave float32 → planar, zero-padding a short final frame.
            for (int c = 0; c < ch; ++c) {
                float* plane = reinterpret_cast<float*>(audioFrame->data[c]);
                for (int i = 0; i < frameSize; ++i)
                    plane[i] = i < n ? pendingAudio[static_cast<size_t>(i) * ch + c] : 0.0f;
            }
            audioFrame->pts = audioPts;
            audioPts += frameSize;
            pendingAudio.erase(pendingAudio.begin(),
                               pendingAudio.begin() + static_cast<size_t>(n) * ch);

            const int r = avcodec_send_frame(aenc.get(), audioFrame);
            if (r < 0)
                return makeUnexpected(mediaErr(MediaErrorKind::decode, "audio send_frame", r));
            if (auto d = drain(aenc.get(), astream); !d)
                return d;
            if (padFinal && pendingAudio.empty())
                return {};
        }
    }
};

Mp4Writer::~Mp4Writer() = default;
const std::string& Mp4Writer::videoEncoderName() const { return impl_->encoderName; }
std::int64_t Mp4Writer::videoFramesWritten() const { return impl_->videoPts; }
std::int64_t Mp4Writer::audioSamplesWritten() const { return impl_->audioAccepted; }

Expected<std::unique_ptr<Mp4Writer>, MediaError>
Mp4Writer::create(const std::filesystem::path& out, const ExportFormat& fmt) {
    using Ret = Expected<std::unique_ptr<Mp4Writer>, MediaError>;
    auto writer = std::unique_ptr<Mp4Writer>(new Mp4Writer());
    writer->impl_ = std::make_unique<Impl>();
    Impl& im = *writer->impl_;
    im.fmt = fmt;

    int r = avformat_alloc_output_context2(&im.oc, nullptr, "mp4", out.string().c_str());
    if (r < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::io, "alloc output", r))};

    // --- video encoder: hardware first, openh264 as the universal fallback.
    std::vector<const char*> candidates;
    if (fmt.preferHardwareEncoder) {
        candidates.push_back("h264_nvenc");
        candidates.push_back("h264_qsv");
        candidates.push_back("h264_amf");
    }
    candidates.push_back("libopenh264");

    for (const char* name : candidates) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name);
        if (!codec)
            continue;
        CodecContextPtr enc{avcodec_alloc_context3(codec)};
        enc->width = fmt.width;
        enc->height = fmt.height;
        enc->time_base = {static_cast<int>(fmt.fps.den), static_cast<int>(fmt.fps.num)};
        enc->framerate = {static_cast<int>(fmt.fps.num), static_cast<int>(fmt.fps.den)};
        enc->pix_fmt = AV_PIX_FMT_YUV420P;
        enc->bit_rate = fmt.videoBitrate;
        enc->gop_size = 30;
        enc->colorspace = AVCOL_SPC_BT709;
        enc->color_primaries = AVCOL_PRI_BT709;
        enc->color_trc = AVCOL_TRC_BT709;
        if (im.oc->oformat->flags & AVFMT_GLOBALHEADER)
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(enc.get(), codec, nullptr) == 0) {
            im.venc = std::move(enc);
            im.encoderName = name;
            break;
        }
    }
    if (!im.venc)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::unsupported, "no H.264 encoder"))};

    im.vstream = avformat_new_stream(im.oc, nullptr);
    avcodec_parameters_from_context(im.vstream->codecpar, im.venc.get());
    im.vstream->time_base = im.venc->time_base;

    // --- audio encoder (AAC).
    const AVCodec* aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!aac)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::unsupported, "no AAC encoder"))};
    im.aenc.reset(avcodec_alloc_context3(aac));
    im.aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    im.aenc->sample_rate = fmt.audioRate;
    av_channel_layout_default(&im.aenc->ch_layout, fmt.audioChannels);
    im.aenc->bit_rate = fmt.audioBitrate;
    im.aenc->time_base = {1, fmt.audioRate};
    if (im.oc->oformat->flags & AVFMT_GLOBALHEADER)
        im.aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((r = avcodec_open2(im.aenc.get(), aac, nullptr)) < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::unsupported, "AAC open", r))};

    im.astream = avformat_new_stream(im.oc, nullptr);
    avcodec_parameters_from_context(im.astream->codecpar, im.aenc.get());
    im.astream->time_base = {1, fmt.audioRate};

    // --- staging frames, packet, io, header.
    im.pkt = av_packet_alloc();

    im.encFrame = av_frame_alloc();
    im.encFrame->format = AV_PIX_FMT_YUV420P;
    im.encFrame->width = fmt.width;
    im.encFrame->height = fmt.height;
    if (av_frame_get_buffer(im.encFrame, 0) < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::io, "video staging alloc"))};

    im.audioFrame = av_frame_alloc();
    im.audioFrame->format = AV_SAMPLE_FMT_FLTP;
    im.audioFrame->sample_rate = fmt.audioRate;
    av_channel_layout_default(&im.audioFrame->ch_layout, fmt.audioChannels);
    im.audioFrame->nb_samples = im.aenc->frame_size > 0 ? im.aenc->frame_size : 1024;
    if (av_frame_get_buffer(im.audioFrame, 0) < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::io, "audio staging alloc"))};

    if ((r = avio_open(&im.oc->pb, out.string().c_str(), AVIO_FLAG_WRITE)) < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::io, "avio_open", r))};

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    r = avformat_write_header(im.oc, &opts);
    av_dict_free(&opts);
    if (r < 0)
        return Ret{makeUnexpected(mediaErr(MediaErrorKind::io, "write_header", r))};

    return Ret{std::move(writer)};
}

Expected<void, MediaError> Mp4Writer::writeVideoFrame(const VideoFrame& frame) {
    Impl& im = *impl_;

    VideoFrame src = frame;
    if (src.isHardware()) {
        auto cpu = src.transferToCpu();
        if (!cpu)
            return makeUnexpected(cpu.error());
        src = *cpu;
    }

    im.sws = sws_getCachedContext(im.sws, src.width(), src.height(),
                                  static_cast<AVPixelFormat>(src.pixelFormatInt()),
                                  im.fmt.width, im.fmt.height, AV_PIX_FMT_YUV420P,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!im.sws)
        return makeUnexpected(mediaErr(MediaErrorKind::unsupported, "sws context"));

    if (av_frame_make_writable(im.encFrame) < 0)
        return makeUnexpected(mediaErr(MediaErrorKind::io, "video frame writable"));

    const std::uint8_t* srcData[4] = {src.data(0), src.data(1), src.data(2), src.data(3)};
    const int srcStride[4] = {src.stride(0), src.stride(1), src.stride(2), src.stride(3)};
    sws_scale(im.sws, srcData, srcStride, 0, src.height(), im.encFrame->data,
              im.encFrame->linesize);

    im.encFrame->pts = im.videoPts++;
    return im.encodeVideo(im.encFrame);
}

Expected<void, MediaError> Mp4Writer::writeBlackFrame() {
    Impl& im = *impl_;
    if (av_frame_make_writable(im.encFrame) < 0)
        return makeUnexpected(mediaErr(MediaErrorKind::io, "video frame writable"));
    // Limited-range black for yuv420p.
    for (int y = 0; y < im.fmt.height; ++y)
        std::memset(im.encFrame->data[0] + static_cast<size_t>(y) * im.encFrame->linesize[0], 16,
                    static_cast<size_t>(im.fmt.width));
    for (int p = 1; p <= 2; ++p)
        for (int y = 0; y < im.fmt.height / 2; ++y)
            std::memset(im.encFrame->data[p] + static_cast<size_t>(y) * im.encFrame->linesize[p],
                        128, static_cast<size_t>(im.fmt.width / 2));
    im.encFrame->pts = im.videoPts++;
    return im.encodeVideo(im.encFrame);
}

Expected<void, MediaError> Mp4Writer::writeAudio(const float* interleaved, int frames) {
    Impl& im = *impl_;
    im.pendingAudio.insert(im.pendingAudio.end(), interleaved,
                           interleaved + static_cast<size_t>(frames) * im.fmt.audioChannels);
    im.audioAccepted += frames;
    return im.flushFullAudioFrames(false);
}

Expected<void, MediaError> Mp4Writer::finish() {
    Impl& im = *impl_;
    if (im.finished)
        return {};
    im.finished = true;

    if (auto a = im.flushFullAudioFrames(true); !a)
        return a;

    // Flush both encoders.
    avcodec_send_frame(im.venc.get(), nullptr);
    if (auto d = im.drain(im.venc.get(), im.vstream); !d)
        return d;
    avcodec_send_frame(im.aenc.get(), nullptr);
    if (auto d = im.drain(im.aenc.get(), im.astream); !d)
        return d;

    const int r = av_write_trailer(im.oc);
    if (r < 0)
        return makeUnexpected(mediaErr(MediaErrorKind::io, "write_trailer", r));
    avio_closep(&im.oc->pb);
    return {};
}

} // namespace velocity::media
