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
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Layout and sizing coordinates
    static constexpr int kHeaderWidth = 128;
    static constexpr int kRulerHeight = 28;
    static constexpr int kVideoTrackHeight = 54;
    static constexpr int kAudioTrackHeight = 46;
    static constexpr int kTrackGap = 3;
    static constexpr int kScrollBarSize = 12;

    struct HitResult {
        enum Kind {
            none,
            ruler,
            clipBody,
            clipLeftEdge,
            clipRightEdge,
            fadeInHandle,
            fadeOutHandle,
            trackHeader,
            headerToggleA, // video: visibility · audio: mute
            headerToggleB, // lock
        };
        Kind kind = none;
        size_t trackIdx = 0;
        std::optional<velocity::engine::ClipId> clipId;
        velocity::engine::ClipPtr clip = nullptr;
    };

    HitResult hitTest(const QPoint& pos);
    [[nodiscard]] double tickToX(velocity::Tick tick) const;
    [[nodiscard]] velocity::Tick xToTick(double x) const;
    void updateScrollRanges();
    void centerZoom(double factor, int anchorX);

    // Variable-height track geometry (video and audio rows differ).
    [[nodiscard]] int trackHeight(size_t idx) const;
    [[nodiscard]] int trackTop(size_t idx) const;    // widget y, includes scroll
    [[nodiscard]] int tracksTotalHeight() const;
    [[nodiscard]] int trackIndexAtY(int y) const;
    [[nodiscard]] QRect headerToggleRectA(size_t idx) const;
    [[nodiscard]] QRect headerToggleRectB(size_t idx) const;

    // Snaps `tick` to nearby edit points/playhead/origin within a pixel
    // radius; suppressed while Alt is held. Records the guide for painting.
    [[nodiscard]] velocity::Tick snapTick(velocity::Tick tick,
                                          std::optional<velocity::engine::ClipId> ignore);

    void showClipMenu(const HitResult& hit, const QPoint& globalPos);
    void showTrackMenu(const HitResult& hit, const QPoint& globalPos);
    void showBackgroundMenu(const QPoint& globalPos);

    DocumentSession* session_;
    QScrollBar* hScrollBar_;
    QScrollBar* vScrollBar_;
    class WaveformCache* waveforms_;

    // Viewing state
    double pixelsPerSecond_ = 120.0; // zoom factor
    velocity::Tick scrollOffsetTicks_ = 0;
    int vScrollOffset_ = 0;

    // Interaction state
    enum class InteractionState {
        idle,
        scrubbing,
        draggingClip,
        trimmingHead,
        trimmingTail,
        draggingFadeIn,
        draggingFadeOut,
    };
    InteractionState interaction_ = InteractionState::idle;
    HitResult activeHit_;
    QPoint dragStartPos_;
    velocity::Tick dragStartClipOffset_ = 0;
    velocity::Tick dragStartClipLen_ = 0;

    // Transient paint state
    std::optional<velocity::Tick> snapGuideTick_;
    std::optional<velocity::Tick> dropIndicatorTick_;
    int dropTargetTrack_ = -1;
};

} // namespace velocity::ui
