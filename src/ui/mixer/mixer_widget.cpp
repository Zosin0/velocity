#include "mixer_widget.h"
#include "../playback/playback_controller.h"
#include "../shell/documentsession.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace velocity::ui {

MixerWidget::MixerWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    // Master Channel layout
    auto* channelLayout = new QVBoxLayout();
    channelLayout->setSpacing(6);

    auto* titleLabel = new QLabel("MASTER", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-weight: bold; color: #888888;");
    channelLayout->addWidget(titleLabel);

    // VU Meters (peak, fed by PlaybackController::audioLevels)
    auto* meterLayout = new QHBoxLayout();
    leftMeter_ = new QProgressBar(this);
    leftMeter_->setOrientation(Qt::Vertical);
    leftMeter_->setRange(0, 100);
    leftMeter_->setValue(0);
    leftMeter_->setTextVisible(false);
    leftMeter_->setFixedWidth(10);
    leftMeter_->setStyleSheet(R"(
        QProgressBar {
            background-color: #121212;
            border: 1px solid #2a2a2a;
        }
        QProgressBar::chunk {
            background-color: qlineargradient(x1:0, y1:1, x2:0, y2:0,
                                              stop:0 #10b981, stop:0.7 #f59e0b, stop:0.95 #ef4444);
        }
    )");

    rightMeter_ = new QProgressBar(this);
    rightMeter_->setOrientation(Qt::Vertical);
    rightMeter_->setRange(0, 100);
    rightMeter_->setValue(0);
    rightMeter_->setTextVisible(false);
    rightMeter_->setFixedWidth(10);
    rightMeter_->setStyleSheet(leftMeter_->styleSheet());

    meterLayout->addWidget(leftMeter_);
    meterLayout->addWidget(rightMeter_);

    // Master fader: 0..125 maps to -inf..+6 dB-ish; 100 = unity gain.
    masterSlider_ = new QSlider(Qt::Vertical, this);
    masterSlider_->setRange(0, 125);
    masterSlider_->setValue(100);
    masterSlider_->setFixedHeight(120);
    masterSlider_->setToolTip("Master volume");
    meterLayout->addWidget(masterSlider_);

    channelLayout->addLayout(meterLayout);

    dbLabel_ = new QLabel("0.0 dB", this);
    dbLabel_->setAlignment(Qt::AlignCenter);
    dbLabel_->setStyleSheet("font-size: 11px; color: #a0a0a0;");
    channelLayout->addWidget(dbLabel_);

    layout->addLayout(channelLayout);
    layout->addStretch();

    connect(masterSlider_, &QSlider::valueChanged, this, [this](int val) {
        // Fader taper: linear-in-dB above -60 dB.
        float gain = 0.0f;
        if (val > 0) {
            const double db = (val - 100) * 0.6; // 100 → 0 dB, 0 → -60 dB, 125 → +15... clamp
            const double clamped = std::min(db, 6.0);
            gain = static_cast<float>(std::pow(10.0, clamped / 20.0));
            dbLabel_->setText(QString::asprintf("%+.1f dB", clamped));
        } else {
            dbLabel_->setText("-inf dB");
        }
        if (playback_)
            playback_->setMasterGain(gain);
    });

    // Peak-hold decay so meters fall smoothly between level updates.
    decayTimer_ = new QTimer(this);
    decayTimer_->setInterval(50);
    connect(decayTimer_, &QTimer::timeout, this, [this]() {
        leftMeter_->setValue(std::max(0, leftMeter_->value() - 6));
        rightMeter_->setValue(std::max(0, rightMeter_->value() - 6));
        if (leftMeter_->value() == 0 && rightMeter_->value() == 0)
            decayTimer_->stop();
    });
}

void MixerWidget::attachPlayback(PlaybackController* playback) {
    playback_ = playback;
    connect(playback_, &PlaybackController::audioLevels, this, &MixerWidget::onAudioLevels);
    // Push the initial fader position into the controller.
    emit masterSlider_->valueChanged(masterSlider_->value());
}

void MixerWidget::onAudioLevels(float left, float right) {
    // Peak meter with a gentle log curve so speech is visible.
    auto toMeter = [](float peak) {
        if (peak <= 0.0001f)
            return 0;
        const float db = 20.0f * std::log10(peak); // 0 dBFS → 0
        return static_cast<int>(std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f) * 100.0f);
    };
    leftMeter_->setValue(std::max(leftMeter_->value(), toMeter(left)));
    rightMeter_->setValue(std::max(rightMeter_->value(), toMeter(right)));
    if (!decayTimer_->isActive())
        decayTimer_->start();
}

} // namespace velocity::ui
