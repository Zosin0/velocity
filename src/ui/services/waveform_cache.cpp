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

const StereoPeaks* WaveformCache::peaksFor(const std::filesystem::path& asset) {
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
    auto future = QtConcurrent::run([key]() -> StereoPeaks {
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
        StereoPeaks peaks;
        peaks.left.resize(binCount);
        peaks.right.resize(binCount);

        std::vector<float> block(static_cast<size_t>(kBin) * 2);
        for (int b = 0; b < binCount; ++b) {
            if (!(*dec)->readAt(static_cast<std::int64_t>(b) * kBin, block.data(), kBin))
                break;
            float peakL = 0.0f, peakR = 0.0f;
            for (int i = 0; i < kBin; ++i) {
                peakL = std::max(peakL, std::abs(block[static_cast<size_t>(i) * 2]));
                peakR = std::max(peakR, std::abs(block[static_cast<size_t>(i) * 2 + 1]));
            }
            peaks.left[b] = std::min(peakL, 1.0f);
            peaks.right[b] = std::min(peakR, 1.0f);
        }
        return peaks;
    });

    auto* watcher = new QFutureWatcher<StereoPeaks>(this);
    connect(watcher, &QFutureWatcher<StereoPeaks>::finished, this, [this, watcher, key] {
        cache_[key] = watcher->result();
        inFlight_[key] = false;
        watcher->deleteLater();
        emit waveformReady(key);
    });
    watcher->setFuture(future);
}

} // namespace velocity::ui
