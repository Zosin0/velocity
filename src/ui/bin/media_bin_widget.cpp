#include "media_bin_widget.h"
#include "../shell/documentsession.h"

#include <velocity/media/probe.h>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace velocity::ui {

namespace {
const char* kImportFilter =
    "Media Files (*.mp4 *.mov *.mkv *.webm *.avi *.m4v "
    "*.mp3 *.wav *.flac *.aac *.ogg "
    "*.png *.jpg *.jpeg *.webp *.bmp);;All Files (*)";
} // namespace

QMimeData* MediaListWidget::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    for (const auto* item : items)
        urls << QUrl::fromLocalFile(item->data(Qt::UserRole).toString());
    mime->setUrls(urls);
    return mime;
}

MediaBinWidget::MediaBinWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search media…");
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MediaBinWidget::applyFilter);

    listWidget_ = new MediaListWidget(this);
    listWidget_->setAlternatingRowColors(true);
    listWidget_->setDragEnabled(true);
    listWidget_->setDefaultDropAction(Qt::CopyAction);
    listWidget_->setToolTip(
        "Drag an asset onto the timeline, or double-click to append it to a track");
    layout->addWidget(listWidget_);

    importButton_ = new QPushButton("Import Media", this);
    layout->addWidget(importButton_);

    connect(importButton_, &QPushButton::clicked, this, &MediaBinWidget::onImportClicked);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, &MediaBinWidget::onItemDoubleClicked);

    // Populate from clips already on the timeline (e.g., reopened projects).
    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr& snapshot) {
        for (const auto& track : snapshot->tracks)
            for (const auto& clip : track->clips)
                addAsset(QString::fromStdWString(clip->asset.wstring()));
    });
}

void MediaBinWidget::addAsset(const QString& filePath) {
    if (filePath.isEmpty())
        return;
    for (int i = 0; i < listWidget_->count(); ++i)
        if (listWidget_->item(i)->data(Qt::UserRole).toString() == filePath)
            return;

    const QFileInfo info(filePath);
    auto* item = new QListWidgetItem(info.fileName(), listWidget_);
    item->setData(Qt::UserRole, filePath);

    // Tooltip with probed metadata (fast header-only probe, docs/04).
    QString tip = filePath;
    if (auto probed = media::probe(std::filesystem::path(filePath.toStdWString()))) {
        if (probed->bestVideo)
            tip += QString("\n%1×%2 %3")
                       .arg(probed->bestVideo->width)
                       .arg(probed->bestVideo->height)
                       .arg(QString::fromStdString(probed->bestVideo->codecName));
        if (probed->bestAudio)
            tip += QString("\n%1 Hz %2")
                       .arg(probed->bestAudio->sampleRate)
                       .arg(QString::fromStdString(probed->bestAudio->codecName));
        tip += QString("\n%1 s").arg(probed->durationSeconds, 0, 'f', 2);
    }
    item->setToolTip(tip);
    applyFilter(searchEdit_->text());
}

void MediaBinWidget::applyFilter(const QString& text) {
    for (int i = 0; i < listWidget_->count(); ++i) {
        auto* item = listWidget_->item(i);
        item->setHidden(!text.isEmpty() &&
                        !item->text().contains(text, Qt::CaseInsensitive));
    }
}

void MediaBinWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() && event->source() != listWidget_)
        event->acceptProposedAction();
}

void MediaBinWidget::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            addAsset(url.toLocalFile());
    event->acceptProposedAction();
}

void MediaBinWidget::onImportClicked() {
    const QStringList files =
        QFileDialog::getOpenFileNames(this, "Import Media Files", QString(), kImportFilter);
    for (const QString& file : files)
        addAsset(file);
    // Import adds to the bin only; placement is drag/double-click (real
    // editors don't dump every import onto the timeline).
}

void MediaBinWidget::onItemDoubleClicked() {
    QListWidgetItem* item = listWidget_->currentItem();
    if (!item)
        return;
    const std::filesystem::path stdPath = item->data(Qt::UserRole).toString().toStdWString();
    session_->importMedia(stdPath, session_->selectedTrackIdx().value_or(0));
}

} // namespace velocity::ui
