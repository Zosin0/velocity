#pragma once
// Real export workflow (docs/10, docs/11): settings → worker-thread render →
// progress/cancel → post-export verification against the doc-10 gates.

#include <velocity/engine/export.h>
#include <velocity/engine/model.h>

#include <QDialog>

#include <atomic>
#include <thread>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace velocity::ui {

class ExportDialog : public QDialog {
    Q_OBJECT

public:
    // Captures the snapshot at open time; immutability makes the worker safe.
    explicit ExportDialog(engine::SnapshotPtr snapshot, QWidget* parent = nullptr);
    ~ExportDialog() override;

signals:
    void progressed(double fraction);
    void exportFinished(bool ok, const QString& summary);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void browseOutput();
    void startExport();
    void cancelExport();
    void onFinished(bool ok, const QString& summary);
    [[nodiscard]] QString verifyOutput(const engine::ExportResult& result,
                                       const engine::ExportSettings& settings) const;

    engine::SnapshotPtr snapshot_;

    QLineEdit* pathEdit_;
    QComboBox* resolutionCombo_;
    QComboBox* qualityCombo_;
    QCheckBox* hwEncodeCheck_;
    QLabel* infoLabel_;
    QProgressBar* progressBar_;
    QPushButton* exportBtn_;
    QPushButton* cancelBtn_;
    QPushButton* closeBtn_;

    std::thread worker_;
    std::atomic<bool> cancelRequested_{false};
    bool exporting_ = false;
};

} // namespace velocity::ui
