#include "timeline_widget.h"
#include "../services/waveform_cache.h"
#include "../shell/documentsession.h"
#include "../shell/icons.h"

#include <velocity/engine/edits.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace velocity::ui {

namespace {
// Timeline palette (dark, quiet — docs/11 §6).
const QColor kCanvasBg(0x14, 0x14, 0x16);
const QColor kTrackBgVideo(0x1c, 0x1c, 0x20);
const QColor kTrackBgAudio(0x1a, 0x1e, 0x1d);
const QColor kTrackBgLocked(0x17, 0x17, 0x18);
const QColor kHeaderBg(0x20, 0x20, 0x24);
const QColor kHeaderBgSelected(0x2a, 0x2a, 0x32);
const QColor kGridLine(0x24, 0x24, 0x28);
const QColor kRulerText(0x9a, 0xa0, 0xa6);
const QColor kVideoClipTop(0x44, 0x62, 0xd6);
const QColor kVideoClipBottom(0x35, 0x4c, 0xb5);
const QColor kAudioClipTop(0x0f, 0xa8, 0x83);
const QColor kAudioClipBottom(0x0a, 0x7d, 0x62);
const QColor kImageClipTop(0x7a, 0x52, 0xd9);
const QColor kImageClipBottom(0x5f, 0x3d, 0xb3);
const QColor kPlayhead(0xff, 0x50, 0x50);
const QColor kSnapGuide(0x4d, 0xab, 0xf7);
const QColor kSelectionBorder(0xff, 0xff, 0xff);
const QColor kWaveform(255, 255, 255, 110);

QString formatRulerTime(double seconds, bool subSecond) {
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    if (!subSecond)
        return QString::asprintf("%02d:%02d", mins, secs);
    const int tenth = static_cast<int>(std::round((seconds - std::floor(seconds)) * 10));
    return QString::asprintf("%02d:%02d.%d", mins, secs, tenth);
}
} // namespace

TimelineWidget::TimelineWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumHeight(160);

    waveforms_ = new WaveformCache(this);
    connect(waveforms_, &WaveformCache::waveformReady, this, [this](const QString&) { update(); });

    hScrollBar_ = new QScrollBar(Qt::Horizontal, this);
    connect(hScrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        scrollOffsetTicks_ = static_cast<Tick>(value) * (kTickRate / 10);
        update();
    });

    vScrollBar_ = new QScrollBar(Qt::Vertical, this);
    connect(vScrollBar_, &QScrollBar::valueChanged, this, [this](int value) {
        vScrollOffset_ = value;
        update();
    });

    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
        updateScrollRanges();
        update();
    });
    connect(session_, &DocumentSession::playheadChanged, this, [this](Tick) { update(); });
    connect(session_, &DocumentSession::selectionChanged, this,
            [this](std::optional<engine::ClipId>) { update(); });

    updateScrollRanges();
}

// ---------------------------------------------------------------- geometry

int TimelineWidget::trackHeight(size_t idx) const {
    auto seq = session_->currentSnapshot();
    if (idx >= seq->tracks.size())
        return kVideoTrackHeight;
    return seq->tracks[idx]->kind == engine::TrackKind::video ? kVideoTrackHeight
                                                              : kAudioTrackHeight;
}

int TimelineWidget::trackTop(size_t idx) const {
    int y = kRulerHeight + kTrackGap - vScrollOffset_;
    for (size_t i = 0; i < idx; ++i)
        y += trackHeight(i) + kTrackGap;
    return y;
}

int TimelineWidget::tracksTotalHeight() const {
    auto seq = session_->currentSnapshot();
    int h = kTrackGap;
    for (size_t i = 0; i < seq->tracks.size(); ++i)
        h += trackHeight(i) + kTrackGap;
    return h;
}

int TimelineWidget::trackIndexAtY(int y) const {
    auto seq = session_->currentSnapshot();
    for (size_t i = 0; i < seq->tracks.size(); ++i) {
        const int top = trackTop(i);
        if (y >= top && y < top + trackHeight(i))
            return static_cast<int>(i);
    }
    return -1;
}

QRect TimelineWidget::headerToggleRectA(size_t idx) const {
    return QRect(kHeaderWidth - 52, trackTop(idx) + trackHeight(idx) - 24, 20, 18);
}

QRect TimelineWidget::headerToggleRectB(size_t idx) const {
    return QRect(kHeaderWidth - 28, trackTop(idx) + trackHeight(idx) - 24, 20, 18);
}

double TimelineWidget::tickToX(Tick tick) const {
    const double seconds = static_cast<double>(tick - scrollOffsetTicks_) / kTickRate;
    return kHeaderWidth + seconds * pixelsPerSecond_;
}

Tick TimelineWidget::xToTick(double x) const {
    const double seconds = (x - kHeaderWidth) / pixelsPerSecond_;
    return scrollOffsetTicks_ + static_cast<Tick>(seconds * kTickRate);
}

void TimelineWidget::updateScrollRanges() {
    auto seq = session_->currentSnapshot();
    // Horizontal: timeline duration plus breathing room.
    const Tick duration = seq->duration() + 5 * kTickRate;
    const Tick visibleDuration =
        static_cast<Tick>((width() - kHeaderWidth) / pixelsPerSecond_ * kTickRate);
    if (duration > visibleDuration) {
        hScrollBar_->setVisible(true);
        hScrollBar_->setRange(0, static_cast<int>((duration - visibleDuration) / (kTickRate / 10)));
        hScrollBar_->setPageStep(static_cast<int>(visibleDuration / (kTickRate / 10)));
    } else {
        hScrollBar_->setVisible(false);
        hScrollBar_->setValue(0);
        scrollOffsetTicks_ = 0;
    }

    // Vertical: unlimited tracks scroll.
    const int contentH = tracksTotalHeight();
    const int viewH = height() - kRulerHeight - kScrollBarSize;
    if (contentH > viewH) {
        vScrollBar_->setVisible(true);
        vScrollBar_->setRange(0, contentH - viewH);
        vScrollBar_->setPageStep(viewH);
    } else {
        vScrollBar_->setVisible(false);
        vScrollBar_->setValue(0);
        vScrollOffset_ = 0;
    }
}

void TimelineWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    hScrollBar_->setGeometry(0, height() - kScrollBarSize, width() - kScrollBarSize,
                             kScrollBarSize);
    vScrollBar_->setGeometry(width() - kScrollBarSize, kRulerHeight, kScrollBarSize,
                             height() - kRulerHeight - kScrollBarSize);
    updateScrollRanges();
}

// ---------------------------------------------------------------- hit test

TimelineWidget::HitResult TimelineWidget::hitTest(const QPoint& pos) {
    HitResult result;
    auto seq = session_->currentSnapshot();

    if (pos.x() < kHeaderWidth) {
        const int trackIdx = trackIndexAtY(pos.y());
        if (trackIdx >= 0) {
            result.trackIdx = static_cast<size_t>(trackIdx);
            if (headerToggleRectA(result.trackIdx).contains(pos))
                result.kind = HitResult::headerToggleA;
            else if (headerToggleRectB(result.trackIdx).contains(pos))
                result.kind = HitResult::headerToggleB;
            else
                result.kind = HitResult::trackHeader;
        }
        return result;
    }

    if (pos.y() <= kRulerHeight) {
        result.kind = HitResult::ruler;
        return result;
    }

    const int trackIdx = trackIndexAtY(pos.y());
    if (trackIdx < 0)
        return result;

    result.trackIdx = static_cast<size_t>(trackIdx);
    const auto& track = seq->tracks[result.trackIdx];
    const Tick tickAtMouse = xToTick(pos.x());
    const int top = trackTop(result.trackIdx);

    for (const auto& clip : track->clips) {
        const double startX = tickToX(clip->dstStart);
        const double endX = tickToX(clip->dstEnd());
        if (pos.x() < startX - 6 || pos.x() > endX + 6)
            continue;
        if (!clip->contains(tickAtMouse) && std::abs(pos.x() - startX) > 6 &&
            std::abs(pos.x() - endX) > 6)
            continue;

        result.clipId = clip->id;
        result.clip = clip;

        // Fade handles live in the top strip of audio clips.
        if (track->kind == engine::TrackKind::audio && pos.y() < top + 14) {
            const double fadeInX = tickToX(clip->dstStart + clip->fadeIn);
            const double fadeOutX = tickToX(clip->dstEnd() - clip->fadeOut);
            if (std::abs(pos.x() - fadeInX) <= 6) {
                result.kind = HitResult::fadeInHandle;
                return result;
            }
            if (std::abs(pos.x() - fadeOutX) <= 6) {
                result.kind = HitResult::fadeOutHandle;
                return result;
            }
        }

        if (std::abs(pos.x() - startX) <= 6)
            result.kind = HitResult::clipLeftEdge;
        else if (std::abs(pos.x() - endX) <= 6)
            result.kind = HitResult::clipRightEdge;
        else
            result.kind = HitResult::clipBody;
        return result;
    }

    return result;
}

Tick TimelineWidget::snapTick(Tick tick, std::optional<engine::ClipId> ignore) {
    snapGuideTick_.reset();
    if (QApplication::keyboardModifiers() & Qt::AltModifier)
        return tick; // Alt suspends the magnet (docs/11 §3)

    const Tick radius = static_cast<Tick>(8.0 / pixelsPerSecond_ * kTickRate);
    Tick best = tick;
    Tick bestDist = radius + 1;
    auto consider = [&](Tick candidate) {
        const Tick d = std::abs(candidate - tick);
        if (d < bestDist) {
            bestDist = d;
            best = candidate;
        }
    };

    consider(0);
    consider(session_->playhead());
    const auto ignoreGroup =
        ignore ? engine::linkedMembers(*session_->currentSnapshot(), *ignore)
               : std::vector<std::pair<size_t, engine::ClipPtr>>{};
    auto isIgnored = [&](engine::ClipId id) {
        for (const auto& [t, c] : ignoreGroup)
            if (c->id == id)
                return true;
        return false;
    };
    for (const auto& track : session_->currentSnapshot()->tracks) {
        for (const auto& c : track->clips) {
            if (isIgnored(c->id))
                continue;
            consider(c->dstStart);
            consider(c->dstEnd());
        }
    }
    if (bestDist <= radius) {
        snapGuideTick_ = best;
        return best;
    }
    return tick;
}

// ---------------------------------------------------------------- drag & drop

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasUrls())
        return;
    const QPoint pos = event->position().toPoint();
    dropTargetTrack_ = trackIndexAtY(pos.y());
    dropIndicatorTick_ = std::max<Tick>(snapTick(xToTick(pos.x()), std::nullopt), 0);
    update();
    event->acceptProposedAction();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    dropIndicatorTick_.reset();
    dropTargetTrack_ = -1;
    snapGuideTick_.reset();
    update();
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls())
        return;
    const QPoint pos = event->position().toPoint();
    const int track = trackIndexAtY(pos.y());
    const Tick at = std::max<Tick>(snapTick(xToTick(pos.x()), std::nullopt), 0);

    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const std::filesystem::path path = url.toLocalFile().toStdWString();
        session_->importMedia(path, track >= 0 ? static_cast<size_t>(track) : 0, at);
    }
    dropIndicatorTick_.reset();
    dropTargetTrack_ = -1;
    snapGuideTick_.reset();
    event->acceptProposedAction();
}

// ---------------------------------------------------------------- zooming

void TimelineWidget::centerZoom(double factor, int anchorX) {
    const Tick anchorTick = xToTick(anchorX);
    pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * factor, 5.0, 2000.0);
    // Keep the tick under the cursor stationary.
    const double seconds = (anchorX - kHeaderWidth) / pixelsPerSecond_;
    scrollOffsetTicks_ =
        std::max<Tick>(anchorTick - static_cast<Tick>(seconds * kTickRate), 0);
    updateScrollRanges();
    {
        QSignalBlocker block(hScrollBar_);
        hScrollBar_->setValue(static_cast<int>(scrollOffsetTicks_ / (kTickRate / 10)));
    }
    update();
}

void TimelineWidget::zoomIn() { centerZoom(1.3, kHeaderWidth + (width() - kHeaderWidth) / 2); }
void TimelineWidget::zoomOut() { centerZoom(1.0 / 1.3, kHeaderWidth + (width() - kHeaderWidth) / 2); }

void TimelineWidget::zoomToFit() {
    auto seq = session_->currentSnapshot();
    const Tick duration = seq->duration();
    if (duration > 0) {
        const double w = width() - kHeaderWidth - 40;
        pixelsPerSecond_ =
            std::clamp(w / (static_cast<double>(duration) / kTickRate), 5.0, 2000.0);
    }
    scrollOffsetTicks_ = 0;
    updateScrollRanges();
    {
        QSignalBlocker block(hScrollBar_);
        hScrollBar_->setValue(0);
    }
    update();
}

// ---------------------------------------------------------------- painting

void TimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);

    const int viewWidth = width() - (vScrollBar_->isVisible() ? kScrollBarSize : 0);
    const int viewHeight = height() - (hScrollBar_->isVisible() ? kScrollBarSize : 0);

    painter.fillRect(rect(), kCanvasBg);

    auto seq = session_->currentSnapshot();
    const Tick visibleStartTick = scrollOffsetTicks_;
    const Tick visibleEndTick = xToTick(viewWidth);

    // --- Grid + ruler -------------------------------------------------------
    double secondsPerGrid = 1.0;
    if (pixelsPerSecond_ < 60.0) secondsPerGrid = 5.0;
    if (pixelsPerSecond_ < 18.0) secondsPerGrid = 15.0;
    if (pixelsPerSecond_ < 8.0)  secondsPerGrid = 60.0;
    if (pixelsPerSecond_ > 240.0) secondsPerGrid = 0.5;
    if (pixelsPerSecond_ > 700.0) secondsPerGrid = 0.1;
    const bool subSecond = secondsPerGrid < 1.0;

    const double startSec =
        std::floor(static_cast<double>(visibleStartTick) / kTickRate / secondsPerGrid) *
        secondsPerGrid;
    const double endSec = static_cast<double>(visibleEndTick) / kTickRate;

    painter.setFont(QFont("Segoe UI", 8));
    for (double s = startSec; s <= endSec + secondsPerGrid; s += secondsPerGrid) {
        const double x = tickToX(ticksFromSeconds(s));
        if (x < kHeaderWidth - 1)
            continue;
        // Grid line through the track area.
        painter.setPen(kGridLine);
        painter.drawLine(QPointF(x, kRulerHeight), QPointF(x, viewHeight));
        // Ruler tick + label.
        painter.setPen(QColor(0x55, 0x55, 0x5c));
        painter.drawLine(QPointF(x, kRulerHeight - 7), QPointF(x, kRulerHeight));
        painter.setPen(kRulerText);
        painter.drawText(QPointF(x + 4, kRulerHeight - 9), formatRulerTime(s, subSecond));
        // Minor ticks between grid lines.
        painter.setPen(QColor(0x38, 0x38, 0x3e));
        for (int m = 1; m < 5; ++m) {
            const double mx = tickToX(ticksFromSeconds(s + secondsPerGrid * m / 5.0));
            if (mx >= kHeaderWidth)
                painter.drawLine(QPointF(mx, kRulerHeight - 4), QPointF(mx, kRulerHeight));
        }
    }
    painter.setPen(QColor(0x2c, 0x2c, 0x32));
    painter.drawLine(kHeaderWidth, kRulerHeight, viewWidth, kRulerHeight);

    // --- Tracks -------------------------------------------------------------
    painter.setClipRect(QRect(0, kRulerHeight, viewWidth, viewHeight - kRulerHeight));
    bool anyClips = false;

    for (size_t i = 0; i < seq->tracks.size(); ++i) {
        const auto& track = seq->tracks[i];
        const int top = trackTop(i);
        const int h = trackHeight(i);
        if (top + h < kRulerHeight || top > viewHeight)
            continue;
        const bool isVideo = track->kind == engine::TrackKind::video;
        const bool isSelectedTrack =
            session_->selectedTrackIdx() && *session_->selectedTrackIdx() == i;

        // Lane background.
        QColor laneBg = track->locked ? kTrackBgLocked : (isVideo ? kTrackBgVideo : kTrackBgAudio);
        if (dropTargetTrack_ == static_cast<int>(i))
            laneBg = laneBg.lighter(130);
        painter.fillRect(QRect(kHeaderWidth, top, viewWidth - kHeaderWidth, h), laneBg);

        // Header card.
        QRect headerRect(0, top, kHeaderWidth, h);
        painter.fillRect(headerRect, isSelectedTrack ? kHeaderBgSelected : kHeaderBg);
        // Kind accent bar.
        painter.fillRect(QRect(0, top, 3, h), isVideo ? kVideoClipTop : kAudioClipTop);
        painter.setPen(QColor(0x2c, 0x2c, 0x32));
        painter.drawRect(headerRect.adjusted(0, 0, -1, -1));

        // Track name + kind icon.
        painter.setPen(isSelectedTrack ? QColor(0xe8, 0xe8, 0xec) : QColor(0xb6, 0xb6, 0xbe));
        painter.setFont(QFont("Segoe UI", 9, QFont::DemiBold));
        icons::icon(isVideo ? "film" : "music")
            .paint(&painter, QRect(10, top + 7, 14, 14));
        painter.drawText(QRect(30, top + 5, kHeaderWidth - 40, 18),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(track->name));

        // Toggle buttons: visibility/mute + lock.
        const QRect btnA = headerToggleRectA(i);
        const QRect btnB = headerToggleRectB(i);
        auto paintToggle = [&](const QRect& r, const QString& name, bool active,
                               const QColor& activeColor) {
            if (active) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 18));
                painter.drawRoundedRect(r, 3, 3);
            }
            icons::icon(name, active ? activeColor : QColor(0x8a, 0x8a, 0x92))
                .paint(&painter, r.adjusted(3, 2, -3, -2));
        };
        if (isVideo)
            paintToggle(btnA, track->hidden ? "eye-off" : "eye", track->hidden,
                        QColor(0xff, 0xa9, 0x4d));
        else
            paintToggle(btnA, track->muted ? "mute" : "volume", track->muted,
                        QColor(0xff, 0xa9, 0x4d));
        paintToggle(btnB, track->locked ? "lock" : "unlock", track->locked,
                    QColor(0xff, 0x6b, 0x6b));

        // --- Clips ----------------------------------------------------------
        for (const auto& clip : track->clips) {
            anyClips = true;
            if (clip->dstEnd() < visibleStartTick || clip->dstStart > visibleEndTick)
                continue; // virtualized: off-screen clips draw nothing

            const double startX = tickToX(clip->dstStart);
            const double endX = tickToX(clip->dstEnd());
            const QRectF clipRect(startX, top + 2, endX - startX, h - 4);
            const bool isSelected =
                session_->selectedClipId() && *session_->selectedClipId() == clip->id;

            QColor cTop, cBottom;
            if (!isVideo) {
                cTop = kAudioClipTop;
                cBottom = kAudioClipBottom;
            } else if (clip->kind == engine::ClipKind::image) {
                cTop = kImageClipTop;
                cBottom = kImageClipBottom;
            } else {
                cTop = kVideoClipTop;
                cBottom = kVideoClipBottom;
            }
            if (isSelected) {
                cTop = cTop.lighter(125);
                cBottom = cBottom.lighter(125);
            }

            QLinearGradient grad(clipRect.topLeft(), clipRect.bottomLeft());
            grad.setColorAt(0.0, cTop);
            grad.setColorAt(1.0, cBottom);

            QPainterPath body;
            body.addRoundedRect(clipRect, 4, 4);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.fillPath(body, grad);
            painter.setPen(QPen(isSelected ? kSelectionBorder : cBottom.darker(140),
                                isSelected ? 1.6 : 1.0));
            painter.drawPath(body);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setClipPath(body, Qt::IntersectClip);

            // Stereo waveform (audio lanes).
            if (!isVideo) {
                const StereoPeaks* peaks = waveforms_->peaksFor(clip->asset);
                if (peaks && !peaks->isEmpty()) {
                    painter.setPen(kWaveform);
                    const double midTop = top + 2 + (h - 4) * 0.3;
                    const double midBottom = top + 2 + (h - 4) * 0.75;
                    const double chanH = (h - 4) * 0.22;
                    const std::int64_t srcBase =
                        ticksFromPts(clip->srcStartPts, clip->srcTimebase);
                    const int x0 = std::max(static_cast<int>(startX), kHeaderWidth);
                    const int x1 = std::min(static_cast<int>(endX), viewWidth);
                    for (int wx = x0; wx < x1; ++wx) {
                        const Tick t = xToTick(wx);
                        const std::int64_t srcSample = srcBase + (t - clip->dstStart);
                        const int bin = static_cast<int>(
                            srcSample / (kTickRate / WaveformCache::kBinsPerSecond));
                        if (bin < 0 || bin >= peaks->left.size())
                            continue;
                        const double hl = std::max(1.0, peaks->left[bin] * chanH);
                        const double hr = std::max(1.0, peaks->right[bin] * chanH);
                        painter.drawLine(QPointF(wx, midTop - hl), QPointF(wx, midTop + hl));
                        painter.drawLine(QPointF(wx, midBottom - hr),
                                         QPointF(wx, midBottom + hr));
                    }
                } else {
                    painter.setPen(QColor(255, 255, 255, 45));
                    painter.drawLine(QPointF(startX + 4, top + h / 2.0),
                                     QPointF(endX - 4, top + h / 2.0));
                }

                // Fade ramps + handles.
                painter.setRenderHint(QPainter::Antialiasing, true);
                if (clip->fadeIn > 0 || clip->fadeOut > 0) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(0, 0, 0, 90));
                    if (clip->fadeIn > 0) {
                        const double fx = tickToX(clip->dstStart + clip->fadeIn);
                        QPainterPath ramp(QPointF(startX, top + 2));
                        ramp.lineTo(fx, top + 2);
                        ramp.lineTo(startX, top + h - 2);
                        ramp.closeSubpath();
                        painter.drawPath(ramp);
                    }
                    if (clip->fadeOut > 0) {
                        const double fx = tickToX(clip->dstEnd() - clip->fadeOut);
                        QPainterPath ramp(QPointF(endX, top + 2));
                        ramp.lineTo(fx, top + 2);
                        ramp.lineTo(endX, top + h - 2);
                        ramp.closeSubpath();
                        painter.drawPath(ramp);
                    }
                }
                // Handles (small dots at the fade apex).
                painter.setBrush(QColor(255, 255, 255, isSelected ? 255 : 160));
                painter.setPen(Qt::NoPen);
                painter.drawEllipse(
                    QPointF(tickToX(clip->dstStart + clip->fadeIn), top + 8), 3.5, 3.5);
                painter.drawEllipse(
                    QPointF(tickToX(clip->dstEnd() - clip->fadeOut), top + 8), 3.5, 3.5);
                painter.setRenderHint(QPainter::Antialiasing, false);
            }

            // Muted / hidden dim.
            if (clip->mute || clip->hidden)
                painter.fillRect(clipRect, QColor(0, 0, 0, 120));

            // Label strip: kind icon + name (+ link glyph when A/V-linked).
            if (clipRect.width() > 36) {
                painter.setPen(QColor(255, 255, 255, 230));
                painter.setFont(QFont("Segoe UI", 8, QFont::DemiBold));
                const QString name =
                    QFileInfo(QString::fromStdWString(clip->asset.wstring())).fileName();
                QString label = name;
                if (clip->isLinked())
                    label += clip->kind == engine::ClipKind::audio ? "  ⛓" : "  ⛓";
                painter.drawText(clipRect.adjusted(7, 3, -7, 0),
                                 Qt::AlignLeft | Qt::AlignTop, label);
            }

            // Trim handles on the selected clip.
            if (isSelected && clipRect.width() > 18) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 200));
                painter.drawRect(QRectF(startX + 1.5, top + h / 2.0 - 7, 3, 14));
                painter.drawRect(QRectF(endX - 4.5, top + h / 2.0 - 7, 3, 14));
            }

            painter.setClipRect(QRect(0, kRulerHeight, viewWidth, viewHeight - kRulerHeight));
        }

        // Locked overlay stripe.
        if (track->locked) {
            painter.setPen(QColor(255, 255, 255, 14));
            for (int sx = kHeaderWidth - h; sx < viewWidth; sx += 14)
                painter.drawLine(sx, top + h, sx + h, top);
        }
    }

    // Empty-state hint.
    if (!anyClips) {
        painter.setPen(QColor(0x6a, 0x6a, 0x72));
        painter.setFont(QFont("Segoe UI", 10));
        painter.drawText(QRect(kHeaderWidth, kRulerHeight, viewWidth - kHeaderWidth,
                               viewHeight - kRulerHeight),
                         Qt::AlignCenter,
                         "Drop media here or double-click an asset in the Media panel");
    }

    // Drop indicator.
    if (dropIndicatorTick_) {
        const double x = tickToX(*dropIndicatorTick_);
        painter.setPen(QPen(kSnapGuide, 2));
        painter.drawLine(QPointF(x, kRulerHeight), QPointF(x, viewHeight));
    }

    // Snap guide during gestures.
    if (snapGuideTick_ && interaction_ != InteractionState::idle) {
        const double x = tickToX(*snapGuideTick_);
        painter.setPen(QPen(kSnapGuide, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, kRulerHeight), QPointF(x, viewHeight));
    }

    painter.setClipping(false);

    // --- Playhead ------------------------------------------------------------
    const double playheadX = tickToX(session_->playhead());
    if (playheadX >= kHeaderWidth - 8 && playheadX <= viewWidth + 8) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(kPlayhead, 1.4));
        painter.drawLine(QPointF(playheadX, 4), QPointF(playheadX, viewHeight));
        QPainterPath cap;
        cap.moveTo(playheadX - 6, 2);
        cap.lineTo(playheadX + 6, 2);
        cap.lineTo(playheadX + 6, kRulerHeight - 12);
        cap.lineTo(playheadX, kRulerHeight - 4);
        cap.lineTo(playheadX - 6, kRulerHeight - 12);
        cap.closeSubpath();
        painter.setBrush(kPlayhead);
        painter.setPen(Qt::NoPen);
        painter.drawPath(cap);
        painter.setRenderHint(QPainter::Antialiasing, false);
    }
}

// ---------------------------------------------------------------- interaction

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;

    HitResult hit = hitTest(event->pos());
    activeHit_ = hit;
    dragStartPos_ = event->pos();

    switch (hit.kind) {
    case HitResult::ruler:
        interaction_ = InteractionState::scrubbing;
        session_->setPlayhead(std::max<Tick>(xToTick(event->pos().x()), 0));
        break;
    case HitResult::headerToggleA: {
        auto seq = session_->currentSnapshot();
        const bool isVideo = seq->tracks[hit.trackIdx]->kind == engine::TrackKind::video;
        session_->updateTrack(hit.trackIdx, [isVideo](engine::Track& t) {
            if (isVideo)
                t.hidden = !t.hidden;
            else
                t.muted = !t.muted;
        });
        break;
    }
    case HitResult::headerToggleB:
        session_->updateTrack(hit.trackIdx,
                              [](engine::Track& t) { t.locked = !t.locked; });
        break;
    case HitResult::clipBody:
        interaction_ = InteractionState::draggingClip;
        session_->beginGesture();
        session_->selectClip(hit.clipId, hit.trackIdx);
        dragStartClipOffset_ = hit.clip->dstStart;
        break;
    case HitResult::clipLeftEdge:
        interaction_ = InteractionState::trimmingHead;
        session_->beginGesture();
        session_->selectClip(hit.clipId, hit.trackIdx);
        dragStartClipOffset_ = hit.clip->dstStart;
        dragStartClipLen_ = hit.clip->dstLen;
        break;
    case HitResult::clipRightEdge:
        interaction_ = InteractionState::trimmingTail;
        session_->beginGesture();
        session_->selectClip(hit.clipId, hit.trackIdx);
        dragStartClipOffset_ = hit.clip->dstStart;
        dragStartClipLen_ = hit.clip->dstLen;
        break;
    case HitResult::fadeInHandle:
        interaction_ = InteractionState::draggingFadeIn;
        session_->beginGesture();
        session_->selectClip(hit.clipId, hit.trackIdx);
        break;
    case HitResult::fadeOutHandle:
        interaction_ = InteractionState::draggingFadeOut;
        session_->beginGesture();
        session_->selectClip(hit.clipId, hit.trackIdx);
        break;
    case HitResult::trackHeader:
        session_->selectClip(std::nullopt, hit.trackIdx);
        break;
    case HitResult::none:
        session_->selectClip(std::nullopt, std::nullopt);
        break;
    }
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    switch (interaction_) {
    case InteractionState::scrubbing:
        session_->setPlayhead(std::max<Tick>(xToTick(event->pos().x()), 0));
        return;
    case InteractionState::draggingClip: {
        if (!activeHit_.clip)
            return;
        const double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        const Tick newStart = snapTick(
            std::max<Tick>(dragStartClipOffset_ + ticksFromSeconds(deltaSec), 0),
            activeHit_.clipId);

        // Vertical drag: move to another track of the same kind.
        const int targetTrack = trackIndexAtY(event->pos().y());
        const auto curTrack = session_->selectedTrackIdx();
        if (targetTrack >= 0 && curTrack && static_cast<size_t>(targetTrack) != *curTrack) {
            auto seq = session_->currentSnapshot();
            if (seq->tracks[static_cast<size_t>(targetTrack)]->kind ==
                seq->tracks[*curTrack]->kind) {
                session_->moveSelectedClipToTrack(static_cast<size_t>(targetTrack), newStart);
                return;
            }
        }
        session_->moveSelectedClip(newStart);
        return;
    }
    case InteractionState::trimmingHead: {
        if (!activeHit_.clip)
            return;
        const double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        session_->trimSelectedClipHead(
            snapTick(dragStartClipOffset_ + ticksFromSeconds(deltaSec), activeHit_.clipId));
        return;
    }
    case InteractionState::trimmingTail: {
        if (!activeHit_.clip)
            return;
        const double deltaSec = (event->pos().x() - dragStartPos_.x()) / pixelsPerSecond_;
        session_->trimSelectedClipTail(
            snapTick(dragStartClipOffset_ + dragStartClipLen_ + ticksFromSeconds(deltaSec),
                     activeHit_.clipId));
        return;
    }
    case InteractionState::draggingFadeIn: {
        if (!activeHit_.clip)
            return;
        const Tick t = xToTick(event->pos().x());
        session_->updateSelectedClip([&](engine::Clip& c) {
            c.fadeIn = std::clamp<Tick>(t - c.dstStart, 0, c.dstLen);
        });
        return;
    }
    case InteractionState::draggingFadeOut: {
        if (!activeHit_.clip)
            return;
        const Tick t = xToTick(event->pos().x());
        session_->updateSelectedClip([&](engine::Clip& c) {
            c.fadeOut = std::clamp<Tick>(c.dstEnd() - t, 0, c.dstLen);
        });
        return;
    }
    case InteractionState::idle:
        break;
    }

    // Hover cursor feedback.
    HitResult hit = hitTest(event->pos());
    switch (hit.kind) {
    case HitResult::clipLeftEdge:
    case HitResult::clipRightEdge:
        setCursor(Qt::SizeHorCursor);
        break;
    case HitResult::fadeInHandle:
    case HitResult::fadeOutHandle:
        setCursor(Qt::PointingHandCursor);
        break;
    case HitResult::clipBody:
        setCursor(Qt::SizeAllCursor);
        break;
    case HitResult::headerToggleA:
    case HitResult::headerToggleB:
        setCursor(Qt::PointingHandCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    if (interaction_ == InteractionState::draggingClip ||
        interaction_ == InteractionState::trimmingHead ||
        interaction_ == InteractionState::trimmingTail ||
        interaction_ == InteractionState::draggingFadeIn ||
        interaction_ == InteractionState::draggingFadeOut) {
        session_->endGesture();
    }
    interaction_ = InteractionState::idle;
    snapGuideTick_.reset();
    setCursor(Qt::ArrowCursor);
    update();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom anchored at the cursor.
        centerZoom(event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2,
                   static_cast<int>(event->position().x()));
        event->accept();
        return;
    }
    if ((event->modifiers() & Qt::ShiftModifier) && vScrollBar_->isVisible()) {
        vScrollBar_->setValue(vScrollBar_->value() - event->angleDelta().y() / 4);
        event->accept();
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    hScrollBar_->setValue(std::clamp(hScrollBar_->value() - steps * 5, hScrollBar_->minimum(),
                                     hScrollBar_->maximum()));
    event->accept();
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        session_->deleteSelectedClip();
        event->accept();
    } else if (event->key() == Qt::Key_S && event->modifiers() == Qt::NoModifier) {
        session_->splitClipAtPlayhead();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

// ---------------------------------------------------------------- context menus

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event) {
    const HitResult hit = hitTest(event->pos());
    if (hit.kind == HitResult::clipBody || hit.kind == HitResult::clipLeftEdge ||
        hit.kind == HitResult::clipRightEdge) {
        session_->selectClip(hit.clipId, hit.trackIdx);
        showClipMenu(hit, event->globalPos());
    } else if (hit.kind == HitResult::trackHeader || hit.kind == HitResult::headerToggleA ||
               hit.kind == HitResult::headerToggleB) {
        showTrackMenu(hit, event->globalPos());
    } else {
        showBackgroundMenu(event->globalPos());
    }
}

void TimelineWidget::showClipMenu(const HitResult& hit, const QPoint& globalPos) {
    auto seq = session_->currentSnapshot();
    const auto& track = seq->tracks[hit.trackIdx];
    QMenu menu(this);

    auto* splitAct = menu.addAction(icons::icon("split"), "Split at Playhead",
                                    [this]() { session_->splitClipAtPlayhead(); });
    splitAct->setEnabled(hit.clip && hit.clip->contains(session_->playhead()));

    const auto members = engine::linkedMembers(*seq, hit.clip->id);
    auto* detachAct = menu.addAction(icons::icon("detach"), "Detach Audio",
                                     [this]() { session_->detachAudioFromSelectedClip(); });
    detachAct->setEnabled(members.size() > 1);

    menu.addSeparator();

    if (track->kind == engine::TrackKind::video) {
        auto* visAct =
            menu.addAction(hit.clip->hidden ? "Show Clip" : "Hide Clip", [this]() {
                session_->updateSelectedClip(
                    [](engine::Clip& c) { c.hidden = !c.hidden; });
            });
        Q_UNUSED(visAct);
    } else {
        menu.addAction(hit.clip->mute ? "Unmute Clip" : "Mute Clip", [this]() {
            session_->updateSelectedClip([](engine::Clip& c) { c.mute = !c.mute; });
        });
    }

    menu.addSeparator();
    menu.addAction(icons::icon("trash"), "Delete",
                   [this]() { session_->deleteSelectedClip(); });
    menu.exec(globalPos);
}

void TimelineWidget::showTrackMenu(const HitResult& hit, const QPoint& globalPos) {
    auto seq = session_->currentSnapshot();
    const auto& track = seq->tracks[hit.trackIdx];
    const bool isVideo = track->kind == engine::TrackKind::video;
    QMenu menu(this);

    if (isVideo)
        menu.addAction(track->hidden ? "Show Track" : "Hide Track", [this, hit]() {
            session_->updateTrack(hit.trackIdx,
                                  [](engine::Track& t) { t.hidden = !t.hidden; });
        });
    else
        menu.addAction(track->muted ? "Unmute Track" : "Mute Track", [this, hit]() {
            session_->updateTrack(hit.trackIdx,
                                  [](engine::Track& t) { t.muted = !t.muted; });
        });
    menu.addAction(track->locked ? "Unlock Track" : "Lock Track", [this, hit]() {
        session_->updateTrack(hit.trackIdx, [](engine::Track& t) { t.locked = !t.locked; });
    });

    menu.addSeparator();
    menu.addAction(icons::icon("plus"), "Add Video Track",
                   [this]() { session_->addTrack(engine::TrackKind::video); });
    menu.addAction(icons::icon("plus"), "Add Audio Track",
                   [this]() { session_->addTrack(engine::TrackKind::audio); });

    menu.addSeparator();
    auto* removeAct = menu.addAction(icons::icon("trash"), "Remove Track", [this, hit]() {
        session_->removeTrack(hit.trackIdx);
    });
    removeAct->setEnabled(track->clips.empty() && !track->locked);
    if (!track->clips.empty())
        removeAct->setToolTip("Track has clips — delete them first");

    menu.exec(globalPos);
}

void TimelineWidget::showBackgroundMenu(const QPoint& globalPos) {
    QMenu menu(this);
    menu.addAction(icons::icon("plus"), "Add Video Track",
                   [this]() { session_->addTrack(engine::TrackKind::video); });
    menu.addAction(icons::icon("plus"), "Add Audio Track",
                   [this]() { session_->addTrack(engine::TrackKind::audio); });
    menu.addSeparator();
    menu.addAction(icons::icon("zoom-fit"), "Zoom to Fit", [this]() { zoomToFit(); });
    menu.exec(globalPos);
}

} // namespace velocity::ui
