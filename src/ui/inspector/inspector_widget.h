#pragma once

#include <QWidget>
#include <functional>
#include <optional>
#include <velocity/engine/model.h>

class QCheckBox;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QGroupBox;
class QSlider;
class QScrollArea;

namespace velocity::ui {

class DocumentSession;

// Property editor for the selected clip. Every control edits the real model
// through DocumentSession::updateSelectedClip (one undo entry per gesture);
// snapshot changes flow back and refresh the controls.
class InspectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit InspectorWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~InspectorWidget() override = default;

private slots:
    void onSelectionChanged(std::optional<velocity::engine::ClipId> clipId);
    void onSnapshotChanged(const velocity::engine::SnapshotPtr& snapshot);
    void updateClipProperties();

private:
    // Slider+spinbox pair bound to one float property with a gesture scope.
    struct FloatControl {
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin = nullptr;
    };
    FloatControl makeFloatControl(QWidget* parent, double min, double max, double step,
                                  const QString& suffix,
                                  const std::function<void(velocity::engine::Clip&, float)>& apply);
    void setFloatControl(const FloatControl& c, double value);

    DocumentSession* session_;
    std::optional<velocity::engine::ClipId> activeClipId_;
    bool refreshing_ = false;

    // UI Widgets
    QScrollArea* scrollArea_;
    QWidget* contentWidget_;
    QLabel* noSelectionLabel_;

    QLabel* clipIdLabel_;
    QLineEdit* assetNameEdit_;
    QDoubleSpinBox* startSpin_;
    QDoubleSpinBox* durationSpin_;

    // Transform controls
    QGroupBox* transformGroup_;
    FloatControl posX_;
    FloatControl posY_;
    FloatControl scale_;
    FloatControl rotation_;
    FloatControl opacity_;
    QCheckBox* visibleCheck_;

    // Audio controls
    QGroupBox* audioGroup_;
    FloatControl volume_;
    QCheckBox* muteCheck_;
    QDoubleSpinBox* fadeInSpin_;
    QDoubleSpinBox* fadeOutSpin_;
};

} // namespace velocity::ui
