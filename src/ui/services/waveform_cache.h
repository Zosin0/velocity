#pragma once
// Background waveform peak generation + cache (docs/07 §5, reduced: single
// peak level at a fixed bins-per-second; the min/max/RMS pyramid arrives
// with the zoomable render pass). Keyed by asset path; generation runs on
// QThreadPool and never blocks the UI thread.

#include <QHash>
#include <QObject>
#include <QVector>

#include <filesystem>

namespace velocity::ui {

class WaveformCache : public QObject {
    Q_OBJECT

public:
    static constexpr int kBinsPerSecond = 50;

    explicit WaveformCache(QObject* parent = nullptr);

    // Peaks in [0,1] at kBinsPerSecond, or nullptr while generating (the
    // first call schedules generation; waveformReady fires when done).
    const QVector<float>* peaksFor(const std::filesystem::path& asset);

signals:
    void waveformReady(const QString& assetPath);

private:
    void generate(const QString& key);

    QHash<QString, QVector<float>> cache_;
    QHash<QString, bool> inFlight_;
};

} // namespace velocity::ui
