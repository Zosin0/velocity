#pragma once
// Sequential-locality frame reader (docs/04 §2 "decoder reuse by locality"):
// monotonically advancing requests roll forward with readNext (cheap);
// jumps fall back to an exact seek. Shared by export and preview playback.

#include <velocity/foundation/time.h>
#include <velocity/media/video_decoder.h>

#include <algorithm>
#include <memory>

namespace velocity::media {

class SequentialFrameReader {
public:
    explicit SequentialFrameReader(std::unique_ptr<VideoDecoder> decoder)
        : decoder_(std::move(decoder)) {}

    [[nodiscard]] const VideoStreamInfo& stream() const { return decoder_->stream(); }

    Expected<VideoFrame, MediaError> at(std::int64_t pts) {
        const Rational tb = decoder_->stream().timebase;
        // Cache hit: the requested pts is still inside the last frame's
        // display interval. Sub-frame playhead updates (60 Hz UI over 30 fps
        // media) must not decode — or worse, return — the next frame early.
        if (lastFrame_ && lastPts_ >= 0 && pts >= lastPts_ &&
            (pts == lastPts_ || (lastDur_ > 0 && pts < lastPts_ + lastDur_)))
            return *lastFrame_;
        // "Near" = at most 2 seconds ahead of the last delivered frame.
        // (isNear, not near: <windows.h> defines near/far as macros.)
        const bool isNear = lastPts_ >= 0 && pts >= lastPts_ &&
                            velocity::detail::cmpMul128(pts - lastPts_, tb.num, 2, tb.den) < 0;
        if (isNear) {
            for (int guard = 0; guard < 600; ++guard) {
                auto f = decoder_->readNext();
                if (!f) {
                    if (f.error().isEndOfStream())
                        break; // clamp handled by the seek path below
                    return f;
                }
                if (f->pts() + std::max<std::int64_t>(f->duration(), 0) > pts ||
                    f->pts() >= pts) {
                    remember(*f);
                    return f;
                }
            }
        }
        auto f = decoder_->readFrameAt(pts);
        if (f)
            remember(*f);
        return f;
    }

private:
    void remember(const VideoFrame& f) {
        lastPts_ = f.pts();
        lastDur_ = std::max<std::int64_t>(f.duration(), 0);
        lastFrame_ = f;
    }

    std::unique_ptr<VideoDecoder> decoder_;
    std::int64_t lastPts_ = -1;
    std::int64_t lastDur_ = 0;
    std::optional<VideoFrame> lastFrame_;
};

} // namespace velocity::media
