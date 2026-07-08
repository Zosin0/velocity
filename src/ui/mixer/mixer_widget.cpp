#include "mixer_widget.h"
#include "../playback/playback_controller.h"
#include "../shell/documentsession.h"
#include "../shell/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace velocity::ui {

namespace {
const char* kMeterStyle = R"(
    QProgressBar {
        background-color: #121214;
        border: 1px solid #2a2a2e;
        border-radius: 2px;
    }
    QProgressBar::chunk {
        background-color: qlineargradient(x1:0, y1:1, x2:0, y2:0,
                                          stop:0 #10b981, stop:0.7 #f59e0b, stop:0.95 #ef4444);
    }
)";

// Fader taper shared by track and master strips: 100 = unity, linear-in-dB.
float faderToGain(int value, QString* dbText = nullptr) {
    if (value <= 0) {
        if (dbText)
            *dbText = "-inf dB";
        return 0.0f;
    }
    const double db = std::min((value - 100) * 0.6, 6.0);
    if (dbText)
        *dbText = QString::asprintf("%+.1f dB", db);
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

int gainToFader(float gain) {
    if (gain <= 0.0001f)
        return 0;
    const double db = 20.0 * std::log10(gain);
    return std::clamp(static_cast<int>(std::lround(db / 0.6 + 100)), 0, 125);
}
} // namespace

MixerWidget::MixerWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(10);

    stripLayout_ = new QHBoxLayout();
    stripLayout_->setSpacing(10);
    layout->addLayout(stripLayout_);
    layout->addStretch();

    // --- Master strip -------------------------------------------------------
    auto* masterLayout = new QVBoxLayout();
    masterLayout->setSpacing(4);

    auto* titleLabel = new QLabel("MASTER", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-weight: 600; font-size: 10px; color: #8a8a92; letter-spacing: 1px;");
    masterLayout->addWidget(titleLabel);

    auto* meterLayout = new QHBoxLayout();
    meterLayout->setSpacing(3);
    leftMeter_ = new QProgressBar(this);
    leftMeter_->setOrientation(Qt::Vertical);
    leftMeter_->setRange(0, 100);
    leftMeter_->setValue(0);
    leftMeter_->setTextVisible(false);
    leftMeter_->setFixedWidth(8);
    leftMeter_->setStyleSheet(kMeterStyle);

    rightMeter_ = new QProgressBar(this);
    rightMeter_->setOrientation(Qt::Vertical);
    rightMeter_->setRange(0, 100);
    rightMeter_->setValue(0);
    rightMeter_->setTextVisible(false);
    rightMeter_->setFixedWidth(8);
    rightMeter_->setStyleSheet(kMeterStyle);

    meterLayout->addWidget(leftMeter_);
    meterLayout->addWidget(rightMeter_);

    masterSlider_ = new QSlider(Qt::Vertical, this);
    masterSlider_->setRange(0, 125);
    masterSlider_->setValue(100);
    masterSlider_->setFixedHeight(110);
    masterSlider_->setToolTip("Master volume");
    meterLayout->addWidget(masterSlider_);
    masterLayout->addLayout(meterLayout);

    dbLabel_ = new QLabel("0.0 dB", this);
    dbLabel_->setAlignment(Qt::AlignCenter);
    dbLabel_->setStyleSheet("font-size: 10px; color: #a0a0a8; font-family: Consolas, monospace;");
    masterLayout->addWidget(dbLabel_);

    layout->addLayout(masterLayout);

    connect(masterSlider_, &QSlider::valueChanged, this, [this](int val) {
        QString db;
        const float gain = faderToGain(val, &db);
        dbLabel_->setText(db);
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

    connect(session_, &DocumentSession::snapshotChanged, this,
            [this](const engine::SnapshotPtr&) { rebuildTrackStrips(); });
    rebuildTrackStrips();
}

void MixerWidget::rebuildTrackStrips() {
    auto seq = session_->currentSnapshot();

    // Collect audio track indices in snapshot order.
    std::vector<size_t> audioTracks;
    for (size_t i = 0; i < seq->tracks.size(); ++i)
        if (seq->tracks[i]->kind == engine::TrackKind::audio)
            audioTracks.push_back(i);

    // Same shape → refresh values in place (cheap, keeps drag focus).
    if (audioTracks.size() == strips_.size()) {
        bool sameShape = true;
        for (size_t s = 0; s < strips_.size(); ++s)
            if (strips_[s].trackIdx != audioTracks[s])
                sameShape = false;
        if (sameShape) {
            refreshTrackStrips();
            return;
        }
    }

    for (auto& strip : strips_)
        strip.container->deleteLater();
    strips_.clear();

    for (size_t idx : audioTracks) {
        TrackStrip strip;
        strip.trackIdx = idx;
        strip.container = new QWidget(this);
        auto* v = new QVBoxLayout(strip.container);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);

        strip.nameLabel = new QLabel(QString::fromStdString(seq->tracks[idx]->name),
                                     strip.container);
        strip.nameLabel->setAlignment(Qt::AlignCenter);
        strip.nameLabel->setStyleSheet(
            "font-weight: 600; font-size: 10px; color: #8a8a92; letter-spacing: 1px;");
        v->addWidget(strip.nameLabel);

        strip.fader = new QSlider(Qt::Vertical, strip.container);
        strip.fader->setRange(0, 125);
        strip.fader->setFixedHeight(110);
        strip.fader->setToolTip("Track volume");
        auto* faderRow = new QHBoxLayout();
        faderRow->addStretch();
        faderRow->addWidget(strip.fader);
        faderRow->addStretch();
        v->addLayout(faderRow);

        strip.muteBtn = new QPushButton(strip.container);
        strip.muteBtn->setCheckable(true);
        strip.muteBtn->setFixedSize(24, 20);
        strip.muteBtn->setToolTip("Mute track");
        strip.muteBtn->setIcon(icons::icon("volume"));
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        btnRow->addWidget(strip.muteBtn);
        btnRow->addStretch();
        v->addLayout(btnRow);

        const size_t trackIdx = idx;
        connect(strip.fader, &QSlider::sliderPressed, this,
                [this]() { session_->beginGesture(); });
        connect(strip.fader, &QSlider::sliderReleased, this,
                [this]() { session_->endGesture(); });
        connect(strip.fader, &QSlider::valueChanged, this, [this, trackIdx](int val) {
            if (refreshing_)
                return;
            const float gain = faderToGain(val);
            session_->updateTrack(trackIdx,
                                  [gain](engine::Track& t) { t.gain = gain; });
        });
        connect(strip.muteBtn, &QPushButton::toggled, this, [this, trackIdx](bool on) {
            if (refreshing_)
                return;
            session_->updateTrack(trackIdx, [on](engine::Track& t) { t.muted = on; });
        });

        stripLayout_->addWidget(strip.container);
        strips_.push_back(strip);
    }
    refreshTrackStrips();
}

void MixerWidget::refreshTrackStrips() {
    auto seq = session_->currentSnapshot();
    refreshing_ = true;
    for (auto& strip : strips_) {
        if (strip.trackIdx >= seq->tracks.size())
            continue;
        const auto& track = seq->tracks[strip.trackIdx];
        strip.nameLabel->setText(QString::fromStdString(track->name));
        if (!strip.fader->isSliderDown())
            strip.fader->setValue(gainToFader(track->gain));
        strip.muteBtn->setChecked(track->muted);
        strip.muteBtn->setIcon(icons::icon(track->muted ? "mute" : "volume"));
    }
    refreshing_ = false;
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
