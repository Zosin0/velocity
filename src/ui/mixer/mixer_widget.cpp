#include "mixer_widget.h"
#include "../shell/documentsession.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <cstdlib> // rand

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

    // VU Meters
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
    rightMeter_->setStyleSheet(leftMeter_->styleSheet()); // share stylesheet

    meterLayout->addWidget(leftMeter_);
    meterLayout->addWidget(rightMeter_);

    // Slider
    masterSlider_ = new QSlider(Qt::Vertical, this);
    masterSlider_->setRange(0, 100);
    masterSlider_->setValue(80);
    masterSlider_->setFixedHeight(120);
    meterLayout->addWidget(masterSlider_);

    channelLayout->addLayout(meterLayout);

    dbLabel_ = new QLabel("0.0 dB", this);
    dbLabel_->setAlignment(Qt::AlignCenter);
    dbLabel_->setStyleSheet("font-size: 11px; color: #a0a0a0;");
    channelLayout->addWidget(dbLabel_);

    layout->addLayout(channelLayout);
    layout->addStretch();

    // Volume level label update
    connect(masterSlider_, &QSlider::valueChanged, this, [this](int val) {
        double db = (val - 80) * 0.25; // 80 is 0.0 dB
        if (val == 0) {
            dbLabel_->setText("-inf dB");
        } else {
            dbLabel_->setText(QString::asprintf("%+.1f dB", db));
        }
    });

    // Animate meters to simulate playback activity
    meterTimer_ = new QTimer(this);
    connect(meterTimer_, &QTimer::timeout, this, &MixerWidget::updateMeters);
    meterTimer_->start(50); // 20 FPS updates
}

void MixerWidget::updateMeters() {
    static int prevPlayhead = 0;
    int curPlayhead = static_cast<int>(session_->playhead());

    // If playhead changed, we assume active playback or scrubbing
    bool isMoving = curPlayhead != prevPlayhead;
    prevPlayhead = curPlayhead;

    if (isMoving && masterSlider_->value() > 0) {
        // Generate random sound level peaks relative to slider volume
        int maxVal = masterSlider_->value();
        int l = std::max(0, maxVal - (std::rand() % 30));
        int r = std::max(0, maxVal - (std::rand() % 30));
        leftMeter_->setValue(l);
        rightMeter_->setValue(r);
    } else {
        // Fast decay to zero
        leftMeter_->setValue(std::max(0, leftMeter_->value() - 15));
        rightMeter_->setValue(std::max(0, rightMeter_->value() - 15));
    }
}

} // namespace velocity::ui
