#include "waveform_cache.h"

#include <velocity/foundation/time.h>
#include <velocity/media/audio_decoder.h>
#include <velocity/media/probe.h>

#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace velocity::ui {

WaveformCache::WaveformCache(QObject* parent) : QObject(parent) {}

const QVector<float>* WaveformCache::peaksFor(const std::filesystem::path& asset) {
    const QString key = QString::fromStdWString(asset.wstring());
    if (auto it = cache_.constFind(key); it != cache_.constEnd())
        return &it.value();
    if (!inFlight_.value(key, false)) {
        inFlight_[key] = true;
        generate(key);
    }
    return nullptr;
}

void WaveformCache::generate(const QString& key) {
    auto future = QtConcurrent::run([key]() -> QVector<float> {
        const std::filesystem::path path(key.toStdWString());

        auto probeRes = media::probe(path);
        if (!probeRes || !probeRes->bestAudio)
            return {};
        auto dec = media::AudioDecoder::open(path);
        if (!dec)
            return {};

        std::int64_t totalSamples = (*dec)->lengthSamples();
        if (totalSamples <= 0)
            totalSamples = static_cast<std::int64_t>(probeRes->durationSeconds * kTickRate);
        if (totalSamples <= 0)
            return {};

        constexpr int kBin = kTickRate / kBinsPerSecond; // samples per bin
        const auto binCount = static_cast<int>((totalSamples + kBin - 1) / kBin);
        QVector<float> peaks(binCount, 0.0f);

        std::vector<float> block(static_cast<size_t>(kBin) * 2);
        for (int b = 0; b < binCount; ++b) {
            if (!(*dec)->readAt(static_cast<std::int64_t>(b) * kBin, block.data(), kBin))
                break;
            float peak = 0.0f;
            for (float s : block)
                peak = std::max(peak, std::abs(s));
            peaks[b] = std::min(peak, 1.0f);
        }
        return peaks;
    });

    auto* watcher = new QFutureWatcher<QVector<float>>(this);
    connect(watcher, &QFutureWatcher<QVector<float>>::finished, this, [this, watcher, key] {
        cache_[key] = watcher->result();
        inFlight_[key] = false;
        watcher->deleteLater();
        emit waveformReady(key);
    });
    watcher->setFuture(future);
}

} // namespace velocity::ui
