#pragma once

#include <QWidget>

#include <cstddef>
#include <vector>

class QSlider;
class QProgressBar;
class QLabel;
class QTimer;
class QHBoxLayout;
class QPushButton;

namespace velocity::ui {

class DocumentSession;
class PlaybackController;

// Mixer panel (docs/07 §6, reduced): one fader strip per audio track
// (fader = Track::gain, mute button) plus the master strip with real peak
// meters. A pure view over the snapshot: strips rebuild when the audio
// track list changes and push edits back through the session.
class MixerWidget : public QWidget {
    Q_OBJECT

public:
    explicit MixerWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~MixerWidget() override = default;

    // Feeds real peak levels and receives the master fader value.
    void attachPlayback(PlaybackController* playback);

public slots:
    void onAudioLevels(float left, float right);

private:
    struct TrackStrip {
        size_t trackIdx = 0;
        QWidget* container = nullptr;
        QSlider* fader = nullptr;
        QPushButton* muteBtn = nullptr;
        QLabel* nameLabel = nullptr;
    };

    void rebuildTrackStrips();
    void refreshTrackStrips();

    DocumentSession* session_;
    PlaybackController* playback_ = nullptr;
    QHBoxLayout* stripLayout_ = nullptr;
    std::vector<TrackStrip> strips_;
    QSlider* masterSlider_;
    QProgressBar* leftMeter_;
    QProgressBar* rightMeter_;
    QLabel* dbLabel_;
    QTimer* decayTimer_;
    bool refreshing_ = false;
};

} // namespace velocity::ui
