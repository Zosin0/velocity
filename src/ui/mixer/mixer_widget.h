#pragma once

#include <QWidget>

class QSlider;
class QProgressBar;
class QLabel;
class QTimer;

namespace velocity::ui {

class DocumentSession;
class PlaybackController;

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
    DocumentSession* session_;
    PlaybackController* playback_ = nullptr;
    QSlider* masterSlider_;
    QProgressBar* leftMeter_;
    QProgressBar* rightMeter_;
    QLabel* dbLabel_;
    QTimer* decayTimer_;
};

} // namespace velocity::ui
