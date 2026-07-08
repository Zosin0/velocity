#pragma once
// Real playback (docs/05): audio is the master clock. A feeder thread keeps a
// lock-free ring buffer full by mixing the timeline (shared AudioMixer); the
// WASAPI render callback only copies from the ring (docs/07 §1 real-time
// rule); the UI playhead is slaved to IAudioClock at 60 Hz.

#include <velocity/engine/audio_mix.h>
#include <velocity/engine/model.h>

#include <QElapsedTimer>
#include <QObject>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QTimer;

namespace velocity::audio {
class AudioOutput;
}

namespace velocity::ui {

class DocumentSession;

class PlaybackController : public QObject {
    Q_OBJECT

public:
    explicit PlaybackController(DocumentSession* session, QObject* parent = nullptr);
    ~PlaybackController() override;

    [[nodiscard]] bool isPlaying() const { return playing_; }
    [[nodiscard]] bool loopEnabled() const { return loop_; }

    void play();
    void pause();
    void stop(); // pause + return playhead to where play started
    void togglePlayPause();
    void setLoop(bool enabled) { loop_ = enabled; }
    void setMasterGain(float linear) { masterGain_.store(linear); }
    [[nodiscard]] float masterGain() const { return masterGain_.load(); }

signals:
    void playStateChanged(bool playing);
    // Peak levels [0,1] for the mixer meters, emitted ~30 Hz while playing.
    void audioLevels(float left, float right);

private:
    void feederLoop();
    void onUiTick();

    DocumentSession* session_;
    std::unique_ptr<audio::AudioOutput> output_;
    QTimer* uiTimer_;

    // Snapshot handoff to the feeder thread (atomic shared_ptr store/load).
    engine::SnapshotPtr playSeq_;
    std::mutex seqMutex_;

    // SPSC ring: feeder writes, WASAPI callback reads. Sizes are frames
    // (stereo interleaved: 2 floats per frame).
    std::vector<float> ring_;
    std::atomic<std::uint64_t> ringWrite_{0}; // total frames written
    std::atomic<std::uint64_t> ringRead_{0};  // total frames consumed
    std::uint32_t ringFrames_ = 0;

    std::thread feeder_;
    std::atomic<bool> feederRun_{false};
    std::atomic<float> masterGain_{1.0f};
    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};

    engine::AudioMixer mixer_; // feeder-thread only
    Tick feedPos_ = 0;         // feeder-thread only

    bool playing_ = false;
    bool loop_ = false;
    bool audioClockActive_ = false; // false → wall clock is the master
    Tick playStartTick_ = 0;
    double clockStartSeconds_ = 0.0; // IAudioClock position when play() ran
    QElapsedTimer wallClock_;        // fallback when no audio endpoint exists
    Tick timelineDuration_ = 0;
};

} // namespace velocity::ui
