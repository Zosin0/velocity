#include "inspector_widget.h"
#include "../shell/documentsession.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace velocity::ui {

InspectorWidget::FloatControl InspectorWidget::makeFloatControl(
    QWidget* parent, double min, double max, double step, const QString& suffix,
    const std::function<void(velocity::engine::Clip&, float)>& apply) {
    FloatControl c;
    c.slider = new QSlider(Qt::Horizontal, parent);
    c.slider->setRange(static_cast<int>(min * 100), static_cast<int>(max * 100));
    c.spin = new QDoubleSpinBox(parent);
    c.spin->setRange(min, max);
    c.spin->setSingleStep(step);
    c.spin->setSuffix(suffix);
    c.spin->setDecimals(2);

    // Slider drag = one gesture = one undo entry.
    connect(c.slider, &QSlider::sliderPressed, this, [this]() { session_->beginGesture(); });
    connect(c.slider, &QSlider::sliderReleased, this, [this]() { session_->endGesture(); });
    connect(c.slider, &QSlider::valueChanged, this, [this, c, apply](int v) {
        if (refreshing_)
            return;
        const double val = v / 100.0;
        {
            QSignalBlocker block(c.spin);
            c.spin->setValue(val);
        }
        session_->updateSelectedClip(
            [&apply, val](engine::Clip& clip) { apply(clip, static_cast<float>(val)); });
    });
    connect(c.spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, c, apply](double val) {
                if (refreshing_)
                    return;
                {
                    QSignalBlocker block(c.slider);
                    c.slider->setValue(static_cast<int>(val * 100));
                }
                session_->updateSelectedClip(
                    [&apply, val](engine::Clip& clip) { apply(clip, static_cast<float>(val)); });
            });
    return c;
}

void InspectorWidget::setFloatControl(const FloatControl& c, double value) {
    QSignalBlocker b1(c.slider);
    QSignalBlocker b2(c.spin);
    c.slider->setValue(static_cast<int>(value * 100));
    c.spin->setValue(value);
}

InspectorWidget::InspectorWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    noSelectionLabel_ = new QLabel("Select a clip on the timeline to inspect properties", this);
    noSelectionLabel_->setAlignment(Qt::AlignCenter);
    noSelectionLabel_->setWordWrap(true);
    noSelectionLabel_->setStyleSheet("color: #888888; padding: 20px;");
    mainLayout->addWidget(noSelectionLabel_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setVisible(false);

    contentWidget_ = new QWidget(scrollArea_);
    auto* contentLayout = new QVBoxLayout(contentWidget_);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(10);

    // --- Clip details -----------------------------------------------------
    auto* basicGroup = new QGroupBox("Clip", contentWidget_);
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setLabelAlignment(Qt::AlignRight);

    assetNameEdit_ = new QLineEdit(basicGroup);
    assetNameEdit_->setReadOnly(true);
    basicLayout->addRow("Asset:", assetNameEdit_);

    clipIdLabel_ = new QLabel("0", basicGroup);
    clipIdLabel_->setStyleSheet("font-family: monospace; color: #a0a0a0;");
    basicLayout->addRow("ID:", clipIdLabel_);

    startSpin_ = new QDoubleSpinBox(basicGroup);
    startSpin_->setRange(0.0, 36000.0);
    startSpin_->setSuffix(" s");
    startSpin_->setDecimals(3);
    startSpin_->setSingleStep(0.1);
    basicLayout->addRow("Start:", startSpin_);

    durationSpin_ = new QDoubleSpinBox(basicGroup);
    durationSpin_->setRange(0.01, 36000.0);
    durationSpin_->setSuffix(" s");
    durationSpin_->setDecimals(3);
    durationSpin_->setSingleStep(0.1);
    basicLayout->addRow("Duration:", durationSpin_);

    contentLayout->addWidget(basicGroup);

    // --- Video transform ---------------------------------------------------
    transformGroup_ = new QGroupBox("Video Transform", contentWidget_);
    auto* transLayout = new QFormLayout(transformGroup_);

    auto addPair = [&](const char* label, FloatControl& c) {
        auto* row = new QHBoxLayout();
        row->addWidget(c.slider, 1);
        row->addWidget(c.spin);
        transLayout->addRow(label, row);
    };

    posX_ = makeFloatControl(transformGroup_, -1.0, 1.0, 0.01, "",
                             [](engine::Clip& c, float v) { c.transform.posX = v; });
    posY_ = makeFloatControl(transformGroup_, -1.0, 1.0, 0.01, "",
                             [](engine::Clip& c, float v) { c.transform.posY = v; });
    scale_ = makeFloatControl(transformGroup_, 0.1, 4.0, 0.05, "×",
                              [](engine::Clip& c, float v) { c.transform.scale = v; });
    rotation_ = makeFloatControl(transformGroup_, -180.0, 180.0, 1.0, "°",
                                 [](engine::Clip& c, float v) { c.transform.rotation = v; });
    opacity_ = makeFloatControl(transformGroup_, 0.0, 1.0, 0.01, "",
                                [](engine::Clip& c, float v) { c.transform.opacity = v; });
    addPair("Position X:", posX_);
    addPair("Position Y:", posY_);
    addPair("Scale:", scale_);
    addPair("Rotation:", rotation_);
    addPair("Opacity:", opacity_);

    visibleCheck_ = new QCheckBox("Visible", transformGroup_);
    connect(visibleCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!refreshing_)
            session_->updateSelectedClip([on](engine::Clip& c) { c.hidden = !on; });
    });
    transLayout->addRow(QString(), visibleCheck_);

    contentLayout->addWidget(transformGroup_);

    // --- Audio --------------------------------------------------------------
    audioGroup_ = new QGroupBox("Audio", contentWidget_);
    auto* audioLayout = new QFormLayout(audioGroup_);

    volume_ = makeFloatControl(audioGroup_, 0.0, 2.0, 0.05, "×",
                               [](engine::Clip& c, float v) { c.gain = v; });
    auto* volRow = new QHBoxLayout();
    volRow->addWidget(volume_.slider, 1);
    volRow->addWidget(volume_.spin);
    audioLayout->addRow("Volume:", volRow);

    muteCheck_ = new QCheckBox("Mute", audioGroup_);
    connect(muteCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!refreshing_)
            session_->updateSelectedClip([on](engine::Clip& c) { c.mute = on; });
    });
    audioLayout->addRow(QString(), muteCheck_);

    fadeInSpin_ = new QDoubleSpinBox(audioGroup_);
    fadeInSpin_->setRange(0.0, 30.0);
    fadeInSpin_->setSuffix(" s");
    fadeInSpin_->setDecimals(2);
    fadeInSpin_->setSingleStep(0.1);
    connect(fadeInSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                if (!refreshing_)
                    session_->updateSelectedClip(
                        [v](engine::Clip& c) { c.fadeIn = ticksFromSeconds(v); });
            });
    audioLayout->addRow("Fade in:", fadeInSpin_);

    fadeOutSpin_ = new QDoubleSpinBox(audioGroup_);
    fadeOutSpin_->setRange(0.0, 30.0);
    fadeOutSpin_->setSuffix(" s");
    fadeOutSpin_->setDecimals(2);
    fadeOutSpin_->setSingleStep(0.1);
    connect(fadeOutSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                if (!refreshing_)
                    session_->updateSelectedClip(
                        [v](engine::Clip& c) { c.fadeOut = ticksFromSeconds(v); });
            });
    audioLayout->addRow("Fade out:", fadeOutSpin_);

    contentLayout->addWidget(audioGroup_);
    contentLayout->addStretch();

    scrollArea_->setWidget(contentWidget_);
    mainLayout->addWidget(scrollArea_);

    connect(session_, &DocumentSession::selectionChanged, this, &InspectorWidget::onSelectionChanged);
    connect(session_, &DocumentSession::snapshotChanged, this, &InspectorWidget::onSnapshotChanged);

    // Placement edits
    connect(startSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (!refreshing_ && activeClipId_)
            session_->moveSelectedClip(ticksFromSeconds(val));
    });
    connect(durationSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (refreshing_ || !activeClipId_)
            return;
        if (auto clip = session_->selectedClip())
            session_->trimSelectedClipTail(clip->dstStart + ticksFromSeconds(val));
    });
}

void InspectorWidget::onSelectionChanged(std::optional<velocity::engine::ClipId> clipId) {
    activeClipId_ = clipId;
    updateClipProperties();
}

void InspectorWidget::onSnapshotChanged(const velocity::engine::SnapshotPtr&) {
    updateClipProperties();
}

void InspectorWidget::updateClipProperties() {
    auto clip = session_->selectedClip();
    if (!clip) {
        activeClipId_ = std::nullopt;
        scrollArea_->setVisible(false);
        noSelectionLabel_->setVisible(true);
        return;
    }
    activeClipId_ = clip->id;

    noSelectionLabel_->setVisible(false);
    scrollArea_->setVisible(true);

    refreshing_ = true;

    clipIdLabel_->setText(QString::number(clip->id));
    assetNameEdit_->setText(QFileInfo(QString::fromStdWString(clip->asset.wstring())).fileName());
    assetNameEdit_->setToolTip(QString::fromStdWString(clip->asset.wstring()));

    {
        QSignalBlocker b1(startSpin_);
        QSignalBlocker b2(durationSpin_);
        startSpin_->setValue(static_cast<double>(clip->dstStart) / kTickRate);
        durationSpin_->setValue(static_cast<double>(clip->dstLen) / kTickRate);
    }

    // Show the group matching the track kind; both for video clips (which may
    // carry audio elsewhere) keeps things simple: video groups on video
    // tracks, audio group always (video clips can have linked audio gain).
    const auto trackIdx = session_->selectedTrackIdx();
    const bool isVideoTrack =
        trackIdx && session_->currentSnapshot()->tracks[*trackIdx]->kind == engine::TrackKind::video;
    transformGroup_->setVisible(isVideoTrack);

    setFloatControl(posX_, clip->transform.posX);
    setFloatControl(posY_, clip->transform.posY);
    setFloatControl(scale_, clip->transform.scale);
    setFloatControl(rotation_, clip->transform.rotation);
    setFloatControl(opacity_, clip->transform.opacity);
    {
        QSignalBlocker b(visibleCheck_);
        visibleCheck_->setChecked(!clip->hidden);
    }

    setFloatControl(volume_, clip->gain);
    {
        QSignalBlocker b1(muteCheck_);
        QSignalBlocker b2(fadeInSpin_);
        QSignalBlocker b3(fadeOutSpin_);
        muteCheck_->setChecked(clip->mute);
        fadeInSpin_->setValue(static_cast<double>(clip->fadeIn) / kTickRate);
        fadeOutSpin_->setValue(static_cast<double>(clip->fadeOut) / kTickRate);
    }

    refreshing_ = false;
}

} // namespace velocity::ui
