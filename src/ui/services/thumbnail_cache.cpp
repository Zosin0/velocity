#include "thumbnail_cache.h"

#include "frame_conversion.h"

#include <velocity/media/probe.h>
#include <velocity/media/video_decoder.h>

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace velocity::ui {

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {}

const MediaCardInfo* ThumbnailCache::infoFor(const QString& filePath) {
    if (auto it = cache_.constFind(filePath); it != cache_.constEnd())
        return &it.value();
    if (!inFlight_.value(filePath, false)) {
        inFlight_[filePath] = true;
        generate(filePath);
    }
    return nullptr;
}

void ThumbnailCache::generate(const QString& key) {
    auto future = QtConcurrent::run([key]() -> MediaCardInfo {
        MediaCardInfo info;
        const std::filesystem::path path(key.toStdWString());

        auto probed = media::probe(path);
        if (!probed)
            return info;

        info.durationSeconds = probed->durationSeconds;
        info.hasVideo = probed->bestVideo.has_value();
        info.hasAudio = probed->bestAudio.has_value();

        QStringList parts;
        if (probed->bestVideo) {
            parts << QString("%1×%2").arg(probed->bestVideo->width).arg(probed->bestVideo->height);
            const auto& fr = probed->bestVideo->frameRate;
            if (fr.num > 0 && fr.den > 0)
                parts << QString("%1 fps").arg(static_cast<double>(fr.num) / fr.den, 0, 'f', 2);
            parts << QString::fromStdString(probed->bestVideo->codecName);
        } else if (probed->bestAudio) {
            parts << QString("%1 Hz").arg(probed->bestAudio->sampleRate)
                  << QString::fromStdString(probed->bestAudio->codecName);
        }
        if (probed->durationSeconds > 0.0)
            parts << QString("%1 s").arg(probed->durationSeconds, 0, 'f', 1);
        info.details = parts.join(QStringLiteral(" · "));

        if (!info.hasVideo)
            return info; // audio-only cards draw an icon instead

        auto dec = media::VideoDecoder::open(path);
        if (!dec)
            return info;

        const Rational tb = (*dec)->stream().timebase;
        const double span = std::clamp(probed->durationSeconds, 0.0, kPreviewSpanSeconds);
        auto scaled = [](const QImage& img) {
            return img.isNull()
                       ? img
                       : img.scaledToWidth(kThumbWidth, Qt::SmoothTransformation);
        };

        // Poster + hover strip in one sequential decode pass.
        const bool animated = probed->durationSeconds > 0.2 && span > 0.0;
        const int frames = animated ? kPreviewFrames : 1;
        for (int i = 0; i < frames; ++i) {
            const double seconds = frames > 1 ? span * i / (frames - 1) : 0.0;
            std::int64_t pts = 0;
            if (tb.num > 0)
                pts = static_cast<std::int64_t>(seconds * tb.den / tb.num);
            auto frame = i == 0 ? (*dec)->readFrameAt(0) : (*dec)->readFrameAt(pts);
            if (!frame)
                break;
            const QImage img = scaled(toQImage(*frame));
            if (img.isNull())
                break;
            if (i == 0)
                info.thumbnail = img;
            info.preview.push_back(img);
        }
        return info;
    });

    auto* watcher = new QFutureWatcher<MediaCardInfo>(this);
    connect(watcher, &QFutureWatcher<MediaCardInfo>::finished, this, [this, watcher, key] {
        cache_[key] = watcher->result();
        inFlight_[key] = false;
        watcher->deleteLater();
        emit cardReady(key);
    });
    watcher->setFuture(future);
}

} // namespace velocity::ui
