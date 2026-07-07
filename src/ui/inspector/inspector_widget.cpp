#include "inspector_widget.h"
#include "../shell/documentsession.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QGroupBox>
#include <QFileInfo>
#include <QMessageBox>

namespace velocity::ui {

InspectorWidget::InspectorWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // No selection label
    noSelectionLabel_ = new QLabel("Select a clip on the timeline to inspect properties", this);
    noSelectionLabel_->setAlignment(Qt::AlignCenter);
    noSelectionLabel_->setWordWrap(true);
    noSelectionLabel_->setStyleSheet("color: #888888; padding: 20px;");
    mainLayout->addWidget(noSelectionLabel_);

    // Create Scroll Area
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setVisible(false);

    // Content container
    contentWidget_ = new QWidget(scrollArea_);
    auto* contentLayout = new QVBoxLayout(contentWidget_);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(10);

    // Basic Properties Group
    auto* basicGroup = new QGroupBox("Clip Details", contentWidget_);
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setLabelAlignment(Qt::AlignRight);
    
    clipIdLabel_ = new QLabel("0", basicGroup);
    clipIdLabel_->setStyleSheet("font-family: monospace; color: #a0a0a0;");
    basicLayout->addRow("Clip ID:", clipIdLabel_);

    assetNameEdit_ = new QLineEdit(basicGroup);
    assetNameEdit_->setReadOnly(true);
    basicLayout->addRow("Asset Name:", assetNameEdit_);

    assetPathEdit_ = new QLineEdit(basicGroup);
    assetPathEdit_->setReadOnly(true);
    basicLayout->addRow("File Path:", assetPathEdit_);

    startSpin_ = new QDoubleSpinBox(basicGroup);
    startSpin_->setRange(0.0, 3600.0);
    startSpin_->setSuffix(" s");
    startSpin_->setSingleStep(0.1);
    basicLayout->addRow("Timeline Start:", startSpin_);

    durationSpin_ = new QDoubleSpinBox(basicGroup);
    durationSpin_->setRange(0.1, 3600.0);
    durationSpin_->setSuffix(" s");
    durationSpin_->setSingleStep(0.1);
    basicLayout->addRow("Duration:", durationSpin_);

    contentLayout->addWidget(basicGroup);

    // Transform Properties Group
    auto* transformGroup = new QGroupBox("Video Transform", contentWidget_);
    auto* transLayout = new QFormLayout(transformGroup);

    // Scale
    auto* scaleLayout = new QHBoxLayout();
    scaleSlider_ = new QSlider(Qt::Horizontal, transformGroup);
    scaleSlider_->setRange(10, 200);
    scaleSlider_->setValue(100);
    scaleSpin_ = new QDoubleSpinBox(transformGroup);
    scaleSpin_->setRange(10.0, 200.0);
    scaleSpin_->setValue(100.0);
    scaleSpin_->setSuffix(" %");
    scaleLayout->addWidget(scaleSlider_);
    scaleLayout->addWidget(scaleSpin_);
    transLayout->addRow("Scale:", scaleLayout);

    // Opacity
    auto* opacityLayout = new QHBoxLayout();
    opacitySlider_ = new QSlider(Qt::Horizontal, transformGroup);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setValue(100);
    opacitySpin_ = new QDoubleSpinBox(transformGroup);
    opacitySpin_->setRange(0.0, 100.0);
    opacitySpin_->setValue(100.0);
    opacitySpin_->setSuffix(" %");
    opacityLayout->addWidget(opacitySlider_);
    opacityLayout->addWidget(opacitySpin_);
    transLayout->addRow("Opacity:", opacityLayout);

    contentLayout->addWidget(transformGroup);
    contentLayout->addStretch();

    scrollArea_->setWidget(contentWidget_);
    mainLayout->addWidget(scrollArea_);

    // Connect signals
    connect(session_, &DocumentSession::selectionChanged, this, &InspectorWidget::onSelectionChanged);
    connect(session_, &DocumentSession::snapshotChanged, this, &InspectorWidget::onSnapshotChanged);

    // Timeline editing hooks
    connect(startSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (activeClipId_) {
            session_->moveSelectedClip(ticksFromSeconds(val));
        }
    });

    connect(durationSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (activeClipId_) {
            auto seq = session_->currentSnapshot();
            for (const auto& track : seq->tracks) {
                for (const auto& clip : track->clips) {
                    if (clip->id == activeClipId_) {
                        session_->trimSelectedClipTail(clip->dstStart + ticksFromSeconds(val));
                        break;
                    }
                }
            }
        }
    });

    // Synchronize slider and spinbox
    connect(scaleSlider_, &QSlider::valueChanged, this, [this](int val) {
        const bool blocked = scaleSpin_->blockSignals(true);
        scaleSpin_->setValue(val);
        scaleSpin_->blockSignals(blocked);
    });
    connect(scaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        const bool blocked = scaleSlider_->blockSignals(true);
        scaleSlider_->setValue(static_cast<int>(val));
        scaleSlider_->blockSignals(blocked);
    });

    connect(opacitySlider_, &QSlider::valueChanged, this, [this](int val) {
        const bool blocked = opacitySpin_->blockSignals(true);
        opacitySpin_->setValue(val);
        opacitySpin_->blockSignals(blocked);
    });
    connect(opacitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        const bool blocked = opacitySlider_->blockSignals(true);
        opacitySlider_->setValue(static_cast<int>(val));
        opacitySlider_->blockSignals(blocked);
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
    if (!activeClipId_) {
        scrollArea_->setVisible(false);
        noSelectionLabel_->setVisible(true);
        return;
    }

    // Search the snapshot for the active clip
    engine::ClipPtr clip = nullptr;
    auto seq = session_->currentSnapshot();
    for (const auto& track : seq->tracks) {
        for (const auto& c : track->clips) {
            if (c->id == activeClipId_) {
                clip = c;
                break;
            }
        }
        if (clip) break;
    }

    if (!clip) {
        activeClipId_ = std::nullopt;
        scrollArea_->setVisible(false);
        noSelectionLabel_->setVisible(true);
        return;
    }

    noSelectionLabel_->setVisible(false);
    scrollArea_->setVisible(true);

    // Temporarily block UI signals during programmatic updates
    const bool startBlocked = startSpin_->blockSignals(true);
    const bool durationBlocked = durationSpin_->blockSignals(true);

    clipIdLabel_->setText(QString::number(clip->id));
    
    QString fullPath = QString::fromStdWString(clip->asset.wstring());
    QFileInfo info(fullPath);
    assetNameEdit_->setText(info.fileName());
    assetPathEdit_->setText(fullPath);

    double startSec = static_cast<double>(clip->dstStart) / kTickRate;
    double durationSec = static_cast<double>(clip->dstLen) / kTickRate;

    startSpin_->setValue(startSec);
    durationSpin_->setValue(durationSec);

    startSpin_->blockSignals(startBlocked);
    durationSpin_->blockSignals(durationBlocked);
}

} // namespace velocity::ui
