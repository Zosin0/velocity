#pragma once

#include <QWidget>
#include <QScrollBar>
#include <optional>
#include <velocity/engine/model.h>

namespace velocity::ui {

class DocumentSession;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~TimelineWidget() override = default;

    // Zooming API
    void zoomIn();
    void zoomOut();
    void zoomToFit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Layout and sizing coordinates
    static constexpr int kHeaderWidth = 70;
    static constexpr int kRulerHeight = 30;
    static constexpr int kTrackHeight = 60;
    static constexpr int kTrackGap = 6;

    struct HitResult {
        enum Kind { none, ruler, clipBody, clipLeftEdge, clipRightEdge, trackHeader };
        Kind kind = none;
        size_t trackIdx = 0;
        std::optional<velocity::engine::ClipId> clipId;
        velocity::engine::ClipPtr clip = nullptr;
    };

    HitResult hitTest(const QPoint& pos);
    double tickToX(velocity::Tick tick) const;
    velocity::Tick xToTick(double x) const;
    void updateScrollRanges();
    [[nodiscard]] int trackIndexAtY(int y) const;
    // Snaps `tick` to nearby edit points/playhead/origin within a pixel radius.
    [[nodiscard]] velocity::Tick snapTick(velocity::Tick tick,
                                          std::optional<velocity::engine::ClipId> ignore) const;

    DocumentSession* session_;
    QScrollBar* hScrollBar_;
    class WaveformCache* waveforms_;

    // Viewing state
    double pixelsPerSecond_ = 120.0; // zoom factor
    velocity::Tick scrollOffsetTicks_ = 0;

    // Interaction state
    enum class InteractionState { idle, scrubbing, draggingClip, trimmingHead, trimmingTail };
    InteractionState interaction_ = InteractionState::idle;
    HitResult activeHit_;
    QPoint dragStartPos_;
    velocity::Tick dragStartClipOffset_ = 0;
    velocity::Tick dragStartClipLen_ = 0;
};

} // namespace velocity::ui
