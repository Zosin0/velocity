#include "timeline_widget.h"
#include "../shell/documentsession.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QFileInfo>
#include <cmath>
#include <algorithm>

namespace velocity::ui {

TimelineWidget::TimelineWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Setup scrollbar
    hScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    hScrollBar_->setRange(0, 100);
    hScrollBar_->setValue(0);

    connect(hScrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        scrollOffsetTicks_ = static_cast<Tick>(value) * (kTickRate / 10); // 10 ticks per step
        update();
    });

    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
        updateScrollRanges();
        update();
    });

    connect(session_, &DocumentSession::playheadChanged, this, [this](Tick) {
        update();
    });

    updateScrollRanges();
}

void TimelineWidget::updateScrollRanges() {
    auto seq = session_->currentSnapshot();
    Tick duration = seq->duration();
    Tick visibleDuration = static_cast<Tick>((width() - kHeaderWidth) / pixelsPerSecond_ * kTickRate);

    if (duration > visibleDuration) {
        // Range covers full timeline duration
        hScrollBar_->setVisible(true);
        hScrollBar_->setRange(0, static_cast<int>((duration - visibleDuration) / (kTickRate / 10)));
        hScrollBar_->setPageStep(static_cast<int>(visibleDuration / (kTickRate / 10)));
    } else {
        hScrollBar_->setVisible(false);
        scrollOffsetTicks_ = 0;
    }
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    hScrollBar_->setGeometry(0, height() - 16, width(), 16);
    updateScrollRanges();
}

double TimelineWidget::tickToX(Tick tick) const {
    double seconds = static_cast<double>(tick - scrollOffsetTicks_) / kTickRate;
    return kHeaderWidth + seconds * pixelsPerSecond_;
}

Tick TimelineWidget::xToTick(double x) const {
    double seconds = (x - kHeaderWidth) / pixelsPerSecond_;
    return scrollOffsetTicks_ + static_cast<Tick>(seconds * kTickRate);
}

TimelineWidget::HitResult TimelineWidget::hitTest(const QPoint& pos) {
    HitResult result;
    if (pos.x() < kHeaderWidth) {
        if (pos.y() > kRulerHeight) {
            int trackIdx = (pos.y() - kRulerHeight) / (kTrackHeight + kTrackGap);
            if (trackIdx >= 0 && trackIdx < static_cast<int>(session_->currentSnapshot()->tracks.size())) {
                result.kind = HitResult::trackHeader;
                result.trackIdx = static_cast<size_t>(trackIdx);
            }
        }
        return result;
    }

    if (pos.y() <= kRulerHeight) {
        result.kind = HitResult::ruler;
        return result;
    }

    int trackIdx = (pos.y() - kRulerHeight) / (kTrackHeight + kTrackGap);
    auto seq = session_->currentSnapshot();
    if (trackIdx >= 0 && trackIdx < static_cast<int>(seq->tracks.size())) {
        result.trackIdx = static_cast<size_t>(trackIdx);
        const auto& track = seq->tracks[trackIdx];
        Tick tickAtMouse = xToTick(pos.x());

        for (const auto& clip : track->clips) {
            if (clip->contains(tickAtMouse)) {
                result.clipId = clip->id;
                result.clip = clip;

                // Check edges for trimming handles (5 pixels tolerance)
                double startX = tickToX(clip->dstStart);
                double endX = tickToX(clip->dstEnd());

                if (std::abs(pos.x() - startX) <= 6) {
                    result.kind = HitResult::clipLeftEdge;
                } else if (std::abs(pos.x() - endX) <= 6) {
                    result.kind = HitResult::clipRightEdge;
                } else {
                    result.kind = HitResult::clipBody;
                }
                return result;
            }
        }
    }

    return result;
}

void TimelineWidget::zoomIn() {
    pixelsPerSecond_ = std::min(1000.0, pixelsPerSecond_ * 1.3);
    updateScrollRanges();
    update();
}

void TimelineWidget::zoomOut() {
    pixelsPerSecond_ = std::max(10.0, pixelsPerSecond_ / 1.3);
    updateScrollRanges();
    update();
}

void TimelineWidget::zoomToFit() {
    auto seq = session_->currentSnapshot();
    Tick duration = seq->duration();
    if (duration > 0) {
        double w = width() - kHeaderWidth - 40;
        pixelsPerSecond_ = w / (static_cast<double>(duration) / kTickRate);
        pixelsPerSecond_ = std::clamp(pixelsPerSecond_, 10.0, 1000.0);
    }
    scrollOffsetTicks_ = 0;
    updateScrollRanges();
    update();
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int viewWidth = width();
    int viewHeight = height() - 16; // space for scrollbar

    // 1. Draw Timeline Background
    painter.fillRect(rect(), QColor(24, 24, 24)); // Dark canvas
    painter.fillRect(QRect(kHeaderWidth, kRulerHeight, viewWidth - kHeaderWidth, viewHeight - kRulerHeight), QColor(15, 15, 15));

    auto seq = session_->currentSnapshot();

    // 2. Draw Ruler markings
    painter.setPen(QColor(60, 60, 60));
    painter.drawLine(kHeaderWidth, kRulerHeight, viewWidth, kRulerHeight);

    // Calculate grid spacing
    double secondsPerGrid = 1.0;
    if (pixelsPerSecond_ < 30.0) secondsPerGrid = 5.0;
    if (pixelsPerSecond_ < 10.0) secondsPerGrid = 10.0;
    if (pixelsPerSecond_ > 250.0) secondsPerGrid = 0.5;

    Tick visibleStartTick = scrollOffsetTicks_;
    Tick visibleEndTick = xToTick(viewWidth);

    double startSec = std::floor(static_cast<double>(visibleStartTick) / kTickRate / secondsPerGrid) * secondsPerGrid;
    double endSec = std::ceil(static_cast<double>(visibleEndTick) / kTickRate);

    painter.setFont(QFont("Segoe UI", 8));
    for (double s = startSec; s <= endSec; s += secondsPerGrid) {
        Tick t = ticksFromSeconds(s);
        double x = tickToX(t);
        if (x < kHeaderWidth) continue;

        // Grid lines
        painter.setPen(QColor(28, 28, 28));
        painter.drawLine(static_cast<int>(x), kRulerHeight, static_cast<int>(x), viewHeight);

        // Ruler ticks
        painter.setPen(QColor(100, 100, 100));
        painter.drawLine(static_cast<int>(x), kRulerHeight - 6, static_cast<int>(x), kRulerHeight);

        // Timecode text (MM:SS)
        int minutes = static_cast<int>(s) / 60;
        int secs = static_cast<int>(s) % 60;
        QString text = QString::asprintf("%02d:%02d", minutes, secs);
        painter.drawText(static_cast<int>(x) + 4, kRulerHeight - 8, text);
    }

    // 3. Draw Track Boundaries and Headers
    for (size_t i = 0; i < seq->tracks.size(); ++i) {
        int trackY = kRulerHeight + static_cast<int>(i) * (kTrackHeight + kTrackGap) + kTrackGap;
        
        // Track background
        painter.fillRect(QRect(kHeaderWidth, trackY, viewWidth - kHeaderWidth, kTrackHeight), QColor(20, 20, 20));

        // Track header background on the left
        bool isSelectedTrack = (session_->selectedTrackIdx() && *session_->selectedTrackIdx() == i);
        painter.fillRect(QRect(0, trackY, kHeaderWidth, kTrackHeight), isSelectedTrack ? QColor(40, 40, 40) : QColor(30, 30, 30));
        painter.setPen(QColor(50, 50, 50));
        painter.drawRect(QRect(0, trackY, kHeaderWidth, kTrackHeight));

        // Track Name
        painter.setPen(isSelectedTrack ? QColor(59, 130, 246) : QColor(200, 200, 200));
        painter.setFont(QFont("Segoe UI", 9, QFont::Bold));
        painter.drawText(QRect(8, trackY, kHeaderWidth - 16, kTrackHeight), Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(seq->tracks[i]->name));
    }

    // 4. Draw Clips
    for (size_t i = 0; i < seq->tracks.size(); ++i) {
        const auto& track = seq->tracks[i];
        int trackY = kRulerHeight + static_cast<int>(i) * (kTrackHeight + kTrackGap) + kTrackGap;

        for (const auto& clip : track->clips) {
            if (clip->dstEnd() < visibleStartTick || clip->dstStart > visibleEndTick) {
                continue; // Virtualized check: clip is off-screen
            }

            double startX = tickToX(clip->dstStart);
            double endX = tickToX(clip->dstEnd());
            double w = endX - startX;

            QRectF clipRect(startX, trackY + 2, w, kTrackHeight - 4);
            
            // Choose color based on track kind and selection
            bool isSelected = (session_->selectedClipId() && *session_->selectedClipId() == clip->id);
            QColor baseColor = (track->kind == engine::TrackKind::video) ? QColor(37, 99, 235, 180) : QColor(16, 185, 129, 180);
            if (isSelected) {
                baseColor = baseColor.lighter(130);
            }

            // Draw Clip Block
            painter.fillRect(clipRect, baseColor);
            
            // Border
            painter.setPen(isSelected ? QColor(255, 255, 255) : baseColor.darker(150));
            painter.drawRect(clipRect);

            // Draw waveform sketches for audio clips
            if (track->kind == engine::TrackKind::audio) {
                painter.setPen(QColor(10, 110, 80, 100));
                // Sketch static waves
                int waveStep = 6;
                for (int wx = static_cast<int>(startX) + 4; wx < static_cast<int>(endX) - 4; wx += waveStep) {
                    int waveH = (kTrackHeight - 12) * (0.3f + 0.6f * std::sin(wx * 0.05f));
                    painter.drawLine(wx, trackY + kTrackHeight / 2 - waveH / 2, wx, trackY + kTrackHeight / 2 + waveH / 2);
                }
            }

            // Draw File Label
            painter.setPen(QColor(255, 255, 255));
            painter.setFont(QFont("Segoe UI", 8, QFont::Medium));
            QString nameText = QFileInfo(QString::fromStdWString(clip->asset.wstring())).fileName();
            painter.drawText(clipRect.adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignTop, nameText);
        }
    }

    // 5. Draw Playhead
    double playheadX = tickToX(session_->playhead());
    if (playheadX >= kHeaderWidth && playheadX <= viewWidth) {
        // Draw vertical red bar
        painter.setPen(QPen(QColor(239, 68, 68), 2));
        painter.drawLine(static_cast<int>(playheadX), 0, static_cast<int>(playheadX), viewHeight);

        // Draw top playhead head (diamond / cap)
        QPolygon cap;
        cap << QPoint(static_cast<int>(playheadX) - 6, 0)
            << QPoint(static_cast<int>(playheadX) + 6, 0)
            << QPoint(static_cast<int>(playheadX) + 6, kRulerHeight - 10)
            << QPoint(static_cast<int>(playheadX), kRulerHeight)
            << QPoint(static_cast<int>(playheadX) - 6, kRulerHeight - 10);
        painter.setBrush(QColor(239, 68, 68));
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(cap);
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        HitResult hit = hitTest(event->pos());
        activeHit_ = hit;
        dragStartPos_ = event->pos();

        if (hit.kind == HitResult::ruler) {
            interaction_ = InteractionState::scrubbing;
            session_->setPlayhead(xToTick(event->pos().x()));
        } else if (hit.kind == HitResult::clipBody) {
            interaction_ = InteractionState::draggingClip;
            session_->beginGesture();
            session_->selectClip(hit.clipId, hit.trackIdx);
            dragStartClipOffset_ = hit.clip->dstStart;
        } else if (hit.kind == HitResult::clipLeftEdge) {
            interaction_ = InteractionState::trimmingHead;
            session_->beginGesture();
            session_->selectClip(hit.clipId, hit.trackIdx);
            dragStartClipOffset_ = hit.clip->dstStart;
            dragStartClipLen_ = hit.clip->dstLen;
        } else if (hit.kind == HitResult::clipRightEdge) {
            interaction_ = InteractionState::trimmingTail;
            session_->beginGesture();
            session_->selectClip(hit.clipId, hit.trackIdx);
            dragStartClipOffset_ = hit.clip->dstStart;
            dragStartClipLen_ = hit.clip->dstLen;
        } else if (hit.kind == HitResult::trackHeader) {
            session_->selectClip(std::nullopt, hit.trackIdx);
        } else {
            session_->selectClip(std::nullopt, std::nullopt);
        }
        update();
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (interaction_ == InteractionState::scrubbing) {
        session_->setPlayhead(xToTick(event->pos().x()));
    } else if (interaction_ == InteractionState::draggingClip && activeHit_.clip) {
        double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        Tick deltaTicks = ticksFromSeconds(deltaSec);
        Tick newStart = dragStartClipOffset_ + deltaTicks;
        session_->moveSelectedClip(newStart);
    } else if (interaction_ == InteractionState::trimmingHead && activeHit_.clip) {
        double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        Tick deltaTicks = ticksFromSeconds(deltaSec);
        Tick newStart = dragStartClipOffset_ + deltaTicks;
        session_->trimSelectedClipHead(newStart);
    } else if (interaction_ == InteractionState::trimmingTail && activeHit_.clip) {
        double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        Tick deltaTicks = ticksFromSeconds(deltaSec);
        Tick newEnd = dragStartClipOffset_ + dragStartClipLen_ + deltaTicks;
        session_->trimSelectedClipTail(newEnd);
    } else {
        // Handle cursor visual feedback on hover
        HitResult hit = hitTest(event->pos());
        if (hit.kind == HitResult::clipLeftEdge || hit.kind == HitResult::clipRightEdge) {
            setCursor(Qt::SizeHorCursor);
        } else if (hit.kind == HitResult::clipBody) {
            setCursor(Qt::SizeAllCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    if (interaction_ == InteractionState::draggingClip ||
        interaction_ == InteractionState::trimmingHead ||
        interaction_ == InteractionState::trimmingTail) {
        session_->endGesture();
    }
    interaction_ = InteractionState::idle;
    setCursor(Qt::ArrowCursor);
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom
        double angle = event->angleDelta().y();
        if (angle > 0) {
            zoomIn();
        } else {
            zoomOut();
        }
        event->accept();
    } else {
        // Scroll horizontally
        int steps = event->angleDelta().y() / 120;
        int newValue = hScrollBar_->value() - steps * 5;
        hScrollBar_->setValue(std::clamp(newValue, hScrollBar_->minimum(), hScrollBar_->maximum()));
        event->accept();
    }
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        session_->deleteSelectedClip();
        event->accept();
    } else if (event->key() == Qt::Key_S) {
        // S = Split Clip
        session_->splitClipAtPlayhead();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

} // namespace velocity::ui
