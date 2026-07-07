#include "export_dialog.h"

#include <velocity/media/probe.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <cmath>

namespace velocity::ui {

ExportDialog::ExportDialog(engine::SnapshotPtr snapshot, QWidget* parent)
    : QDialog(parent), snapshot_(std::move(snapshot)) {
    setWindowTitle("Export Video");
    setModal(true);
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    // Output file
    auto* pathRow = new QHBoxLayout();
    pathEdit_ = new QLineEdit(this);
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    pathEdit_->setText(movies + "/velocity_export.mp4");
    auto* browseBtn = new QPushButton("Browse…", this);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browseOutput);
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browseBtn);
    form->addRow("Output file:", pathRow);

    // Resolution
    resolutionCombo_ = new QComboBox(this);
    resolutionCombo_->addItem(QString("Sequence (%1×%2)").arg(snapshot_->width).arg(snapshot_->height),
                              QSize(0, 0));
    resolutionCombo_->addItem("1920×1080 (Full HD)", QSize(1920, 1080));
    resolutionCombo_->addItem("1280×720 (HD)", QSize(1280, 720));
    resolutionCombo_->addItem("1080×1920 (Vertical)", QSize(1080, 1920));
    form->addRow("Resolution:", resolutionCombo_);

    // Quality
    qualityCombo_ = new QComboBox(this);
    qualityCombo_->addItem("High (16 Mbps)", 16'000'000);
    qualityCombo_->addItem("Medium (8 Mbps)", 8'000'000);
    qualityCombo_->addItem("Low (4 Mbps)", 4'000'000);
    qualityCombo_->setCurrentIndex(1);
    form->addRow("Quality:", qualityCombo_);

    hwEncodeCheck_ = new QCheckBox("Use hardware encoder (NVENC / QuickSync / AMF)", this);
    hwEncodeCheck_->setChecked(true);
    form->addRow(QString(), hwEncodeCheck_);

    layout->addLayout(form);

    // Timeline facts
    const double seconds = static_cast<double>(snapshot_->duration()) / kTickRate;
    const auto frames = engine::expectedFrameCount(snapshot_->duration(), snapshot_->frameRate);
    infoLabel_ = new QLabel(
        QString("Timeline: %1 s · %2 frames @ %3 fps · H.264 + AAC")
            .arg(seconds, 0, 'f', 2)
            .arg(frames)
            .arg(static_cast<double>(snapshot_->frameRate.num) / snapshot_->frameRate.den, 0,
                 'f', 2),
        this);
    infoLabel_->setStyleSheet("color: #9ca3af;");
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    layout->addWidget(progressBar_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    exportBtn_ = new QPushButton("Export", this);
    exportBtn_->setDefault(true);
    cancelBtn_ = new QPushButton("Cancel Export", this);
    cancelBtn_->setEnabled(false);
    closeBtn_ = new QPushButton("Close", this);
    buttons->addWidget(exportBtn_);
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(closeBtn_);
    layout->addLayout(buttons);

    connect(exportBtn_, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(cancelBtn_, &QPushButton::clicked, this, &ExportDialog::cancelExport);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::reject);

    connect(this, &ExportDialog::progressed, this, [this](double f) {
        progressBar_->setValue(static_cast<int>(f * 1000.0));
    });
    connect(this, &ExportDialog::exportFinished, this, &ExportDialog::onFinished);
}

ExportDialog::~ExportDialog() {
    cancelRequested_.store(true);
    if (worker_.joinable())
        worker_.join();
}

void ExportDialog::browseOutput() {
    const QString file = QFileDialog::getSaveFileName(this, "Export Video File",
                                                      pathEdit_->text(), "MPEG-4 Video (*.mp4)");
    if (!file.isEmpty())
        pathEdit_->setText(file);
}

void ExportDialog::startExport() {
    if (exporting_)
        return;

    QString path = pathEdit_->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Export", "Choose an output file first.");
        return;
    }
    if (!path.endsWith(".mp4", Qt::CaseInsensitive))
        path += ".mp4";

    const QFileInfo fi(path);
    if (!fi.dir().exists()) {
        QMessageBox::warning(this, "Export", "The output folder does not exist.");
        return;
    }
    // Overwrite protection for hand-typed paths (Browse… already confirms).
    if (fi.exists()) {
        const auto answer = QMessageBox::question(
            this, "Overwrite?", QString("%1 already exists.\nOverwrite it?").arg(fi.fileName()),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    pathEdit_->setText(path);

    engine::ExportSettings settings;
    const QSize res = resolutionCombo_->currentData().toSize();
    settings.width = res.width();
    settings.height = res.height();
    settings.videoBitrate = qualityCombo_->currentData().toLongLong();
    settings.preferHardwareEncoder = hwEncodeCheck_->isChecked();

    exporting_ = true;
    cancelRequested_.store(false);
    exportBtn_->setEnabled(false);
    cancelBtn_->setEnabled(true);
    closeBtn_->setEnabled(false);
    progressBar_->setValue(0);
    infoLabel_->setText("Exporting…");

    const std::filesystem::path outPath = std::filesystem::path(path.toStdWString());
    worker_ = std::thread([this, outPath, settings] {
        auto result = engine::exportSequence(snapshot_, outPath, settings, [this](double f) {
            emit progressed(f);
            return !cancelRequested_.load();
        });

        if (result) {
            const QString verification = verifyOutput(*result, settings);
            emit exportFinished(true,
                                QString("Export complete — %1 frames, encoder %2.\n%3")
                                    .arg(result->videoFrames)
                                    .arg(QString::fromStdString(result->videoEncoder))
                                    .arg(verification));
        } else {
            // Remove partial output so failed/cancelled exports leave nothing behind.
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
            emit exportFinished(false, QString::fromStdString(result.error()));
        }
    });
}

QString ExportDialog::verifyOutput(const engine::ExportResult& result,
                                   const engine::ExportSettings& settings) const {
    // Post-export verification (docs/10 §4): probe the file and check the
    // gates a user would notice first.
    const auto path = std::filesystem::path(pathEdit_->text().toStdWString());
    auto info = media::probe(path);
    if (!info)
        return "Verification FAILED: output not readable.";

    QStringList problems;
    if (!info->bestVideo)
        problems << "no video stream";
    if (!info->bestAudio)
        problems << "no audio stream";

    const Rational fps = settings.fps.num > 0 ? settings.fps : snapshot_->frameRate;
    const auto expected = engine::expectedFrameCount(snapshot_->duration(), fps);
    if (result.videoFrames != expected)
        problems << QString("frame count %1 ≠ expected %2").arg(result.videoFrames).arg(expected);

    const double expectedSec = static_cast<double>(snapshot_->duration()) / kTickRate;
    if (std::abs(info->durationSeconds - expectedSec) > 0.05)
        problems << QString("duration %1s ≠ expected %2s")
                        .arg(info->durationSeconds, 0, 'f', 3)
                        .arg(expectedSec, 0, 'f', 3);

    return problems.isEmpty()
               ? "Verified: frame count, duration, video + audio streams OK."
               : "Verification FAILED: " + problems.join("; ");
}

void ExportDialog::cancelExport() {
    cancelRequested_.store(true);
    cancelBtn_->setEnabled(false);
    infoLabel_->setText("Cancelling…");
}

void ExportDialog::onFinished(bool ok, const QString& summary) {
    if (worker_.joinable())
        worker_.join();
    exporting_ = false;
    exportBtn_->setEnabled(true);
    cancelBtn_->setEnabled(false);
    closeBtn_->setEnabled(true);
    infoLabel_->setText(summary);
    if (ok) {
        progressBar_->setValue(1000);
    } else {
        progressBar_->setValue(0);
    }
}

void ExportDialog::closeEvent(QCloseEvent* event) {
    if (exporting_) {
        cancelExport();
        event->ignore();
        return;
    }
    event->accept();
}

} // namespace velocity::ui
