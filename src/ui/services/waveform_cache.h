#pragma once
// Background waveform peak generation + cache (docs/07 §5, reduced: one
// per-channel peak level at a fixed bins-per-second; the full min/max/RMS
// pyramid arrives with the zoomable render pass). Keyed by asset path;
// generation runs on QThreadPool and never blocks the UI thread.

#include <QHash>
#include <QObject>
#include <QVector>

#include <filesystem>

namespace velocity::ui {

// Per-channel peaks in [0,1] at WaveformCache::kBinsPerSecond. Mono sources
// carry identical left/right vectors so drawing code has one shape.
struct StereoPeaks {
    QVector<float> left;
    QVector<float> right;

    [[nodiscard]] bool isEmpty() const { return left.isEmpty(); }
};

class WaveformCache : public QObject {
    Q_OBJECT

public:
    static constexpr int kBinsPerSecond = 50;

    explicit WaveformCache(QObject* parent = nullptr);

    // Peaks for the asset, or nullptr while generating (the first call
    // schedules generation; waveformReady fires when done).
    const StereoPeaks* peaksFor(const std::filesystem::path& asset);

signals:
    void waveformReady(const QString& assetPath);

private:
    void generate(const QString& key);

    QHash<QString, StereoPeaks> cache_;
    QHash<QString, bool> inFlight_;
};

} // namespace velocity::ui
