#pragma once
// Background media-card data for the bin (docs/04 §3 import pipeline,
// reduced): poster thumbnail, probed facts line, and a short hover-preview
// frame strip (~2 s) — all generated asynchronously on QThreadPool and
// cached in memory keyed by asset path. Never blocks the UI thread.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVector>

#include <filesystem>

namespace velocity::ui {

struct MediaCardInfo {
    QImage thumbnail;        // poster frame (bounded size), null for audio-only
    QVector<QImage> preview; // hover scrub strip across the first ~2 s
    QString details;         // "1920×1080 · 29.97 fps · h264 · 12.3 s"
    double durationSeconds = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;
};

class ThumbnailCache : public QObject {
    Q_OBJECT

public:
    static constexpr int kThumbWidth = 192;
    static constexpr int kPreviewFrames = 10;
    static constexpr double kPreviewSpanSeconds = 2.0;

    explicit ThumbnailCache(QObject* parent = nullptr);

    // Cached card info, or nullptr while generating (the first call
    // schedules generation; cardReady fires when done).
    const MediaCardInfo* infoFor(const QString& filePath);

signals:
    void cardReady(const QString& filePath);

private:
    void generate(const QString& key);

    QHash<QString, MediaCardInfo> cache_;
    QHash<QString, bool> inFlight_;
};

} // namespace velocity::ui
