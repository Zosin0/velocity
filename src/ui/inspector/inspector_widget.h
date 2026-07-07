#pragma once

#include <QWidget>
#include <optional>
#include <velocity/engine/model.h>

class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QSlider;
class QScrollArea;

namespace velocity::ui {

class DocumentSession;

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
    DocumentSession* session_;
    std::optional<velocity::engine::ClipId> activeClipId_;

    // UI Widgets
    QScrollArea* scrollArea_;
    QWidget* contentWidget_;
    QLabel* noSelectionLabel_;
    
    QLabel* clipIdLabel_;
    QLineEdit* assetNameEdit_;
    QLineEdit* assetPathEdit_;
    QDoubleSpinBox* startSpin_;
    QDoubleSpinBox* durationSpin_;
    
    // Transform controls
    QSlider* scaleSlider_;
    QDoubleSpinBox* scaleSpin_;
    QSlider* opacitySlider_;
    QDoubleSpinBox* opacitySpin_;
};

} // namespace velocity::ui
