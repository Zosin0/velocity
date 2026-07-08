#include "playback_controller.h"

#include "../shell/documentsession.h"

#include <velocity/audio/output.h>

#include <QTimer>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace velocity::ui {

namespace {
constexpr std::uint32_t kRingFrames = 1 << 15; // 32768 frames ≈ 680 ms @ 48k
constexpr Tick kFeedChunk = 4800;              // feed in 100 ms chunks
} // namespace

PlaybackController::PlaybackController(DocumentSession* session, QObject* parent)
    : QObject(parent), session_(session) {
    ring_.resize(static_cast<size_t>(kRingFrames) * 2, 0.0f);
    ringFrames_ = kRingFrames;

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(16); // ~60 Hz playhead updates
    connect(uiTimer_, &QTimer::timeout, this, &PlaybackController::onUiTick);

    auto out = audio::AudioOutput::create();
    if (out) {
        output_ = std::move(out.value());
        if (output_->sampleRate() != static_cast<std::uint32_t>(kTickRate))
            spdlog::warn("audio device mix rate is {} Hz (engine rate {}); playback clock "
                         "compensates but resample quality is device-side",
                         output_->sampleRate(), kTickRate);
    } else {
        spdlog::warn("no audio endpoint: playback will run on a wall clock ({})", out.error());
    }

    // Edits during playback: pause to prevent state inconsistency and background seek overload.
    connect(session_, &DocumentSession::snapshotChanged, this,
            [this](const engine::SnapshotPtr& snap) {
                if (playing_)
                    pause();
                std::lock_guard lock(seqMutex_);
                playSeq_ = snap;
                timelineDuration_ = snap->duration();
            });
}

PlaybackController::~PlaybackController() {
    feederRun_.store(false);
    if (feeder_.joinable())
        feeder_.join();
    if (output_)
        output_->stop();
}

void PlaybackController::play() {
    if (playing_)
        return;
    {
        std::lock_guard lock(seqMutex_);
        playSeq_ = session_->currentSnapshot();
        timelineDuration_ = playSeq_->duration();
    }
    if (timelineDuration_ <= 0)
        return;

    playStartTick_ = std::min(session_->playhead(), timelineDuration_ - 1);
    if (playStartTick_ >= timelineDuration_ - kTickRate / 100)
        playStartTick_ = 0; // at the very end: restart from the top

    feedPos_ = playStartTick_;
    ringWrite_.store(0);
    ringRead_.store(0);
    peakL_.store(0.0f);
    peakR_.store(0.0f);

    feederRun_.store(true);
    feeder_ = std::thread([this] { feederLoop(); });

    wallClock_.start();
    if (output_) {
        const std::uint32_t deviceRate = output_->sampleRate();
        const std::uint32_t channels = output_->channels();
        auto started = output_->start([this, deviceRate, channels](float* buf,
                                                                   std::uint32_t frames) {
            // WASAPI render thread: copy from the ring, engine rate → device
            // rate by nearest-neighbor step (identical when rates match).
            const double step = static_cast<double>(kTickRate) / deviceRate;
            const std::uint64_t rd = ringRead_.load(std::memory_order_acquire);
            const std::uint64_t wr = ringWrite_.load(std::memory_order_acquire);
            const std::uint64_t avail = wr - rd;

            std::uint64_t consumed = 0;
            for (std::uint32_t i = 0; i < frames; ++i) {
                const std::uint64_t src = rd + static_cast<std::uint64_t>(i * step);
                float l = 0.0f, r = 0.0f;
                if (src < wr) {
                    const size_t idx = (src % ringFrames_) * 2;
                    l = ring_[idx];
                    r = ring_[idx + 1];
                    consumed = static_cast<std::uint64_t>(i * step) + 1;
                }
                buf[i * channels] = l;
                if (channels > 1)
                    buf[i * channels + 1] = r;
                for (std::uint32_t c = 2; c < channels; ++c)
                    buf[i * channels + c] = 0.0f;
            }
            ringRead_.store(rd + std::min<std::uint64_t>(consumed, avail),
                            std::memory_order_release);
        });
        if (!started) {
            spdlog::warn("audio start failed: {}", started.error());
        } else {
            if (auto c = output_->clock())
                clockStartSeconds_ = c->positionSeconds;
        }
    }

    playing_ = true;
    uiTimer_->start();
    emit playStateChanged(true);
}

void PlaybackController::pause() {
    if (!playing_)
        return;
    feederRun_.store(false);
    if (feeder_.joinable())
        feeder_.join();
    if (output_)
        output_->stop();
    uiTimer_->stop();
    playing_ = false;
    peakL_.store(0.0f);
    peakR_.store(0.0f);
    emit audioLevels(0.0f, 0.0f);
    emit playStateChanged(false);
}

void PlaybackController::stop() {
    const Tick backTo = playStartTick_;
    pause();
    session_->setPlayhead(backTo);
}

void PlaybackController::togglePlayPause() {
    if (playing_)
        pause();
    else
        play();
}

void PlaybackController::feederLoop() {
    std::vector<float> chunk(static_cast<size_t>(kFeedChunk) * 2);
    while (feederRun_.load(std::memory_order_acquire)) {
        const std::uint64_t wr = ringWrite_.load(std::memory_order_relaxed);
        const std::uint64_t rd = ringRead_.load(std::memory_order_acquire);
        if (wr - rd > static_cast<std::uint64_t>(ringFrames_) -
                          static_cast<std::uint64_t>(kFeedChunk)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        engine::SnapshotPtr seq;
        Tick duration;
        {
            std::lock_guard lock(seqMutex_);
            seq = playSeq_;
            duration = timelineDuration_;
        }
        if (!seq || duration <= 0)
            break;

        // Wrap for loop mode; past the end, feed silence (playback drains).
        Tick mixPos = feedPos_;
        if (loop_ && mixPos >= duration)
            mixPos %= duration;

        const Tick n = std::min<Tick>(kFeedChunk, loop_ ? duration - (mixPos % duration)
                                                        : std::max<Tick>(duration - mixPos, 0));
        if (n <= 0 && !loop_) {
            // End of timeline: feed silence so the device drains cleanly.
            std::fill(chunk.begin(), chunk.end(), 0.0f);
            for (Tick i = 0; i < kFeedChunk; ++i) {
                const size_t idx = ((wr + i) % ringFrames_) * 2;
                ring_[idx] = 0.0f;
                ring_[idx + 1] = 0.0f;
            }
            ringWrite_.store(wr + kFeedChunk, std::memory_order_release);
            feedPos_ += kFeedChunk;
            continue;
        }

        const Tick mixLen = std::max<Tick>(n, 1);
        mixer_.mix(*seq, mixPos, static_cast<int>(mixLen), chunk.data(), masterGain_.load());

        float pl = 0.0f, pr = 0.0f;
        for (Tick i = 0; i < mixLen; ++i) {
            pl = std::max(pl, std::abs(chunk[static_cast<size_t>(i) * 2]));
            pr = std::max(pr, std::abs(chunk[static_cast<size_t>(i) * 2 + 1]));
            const size_t idx = ((wr + static_cast<std::uint64_t>(i)) % ringFrames_) * 2;
            ring_[idx] = chunk[static_cast<size_t>(i) * 2];
            ring_[idx + 1] = chunk[static_cast<size_t>(i) * 2 + 1];
        }
        ringWrite_.store(wr + static_cast<std::uint64_t>(mixLen), std::memory_order_release);
        feedPos_ += mixLen;
        peakL_.store(std::max(peakL_.load(), pl));
        peakR_.store(std::max(peakR_.load(), pr));
    }
}

void PlaybackController::onUiTick() {
    if (!playing_)
        return;

    Tick elapsed = 0;
    if (output_) {
        if (auto c = output_->clock()) {
            elapsed = static_cast<Tick>((c->positionSeconds - clockStartSeconds_) *
                                        static_cast<double>(kTickRate));
        }
    } else {
        elapsed = static_cast<Tick>(wallClock_.elapsed() * (kTickRate / 1000));
    }

    Tick tick = playStartTick_ + elapsed;
    if (timelineDuration_ > 0 && tick >= timelineDuration_) {
        if (loop_) {
            tick = playStartTick_ == 0 ? tick % timelineDuration_
                                       : (tick - playStartTick_) % timelineDuration_;
        } else {
            session_->setPlayhead(timelineDuration_);
            pause();
            return;
        }
    }
    session_->setPlayhead(tick);

    emit audioLevels(peakL_.exchange(0.0f), peakR_.exchange(0.0f));
}

} // namespace velocity::ui
