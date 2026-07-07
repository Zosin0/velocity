#include "media_bin_widget.h"
#include "../shell/documentsession.h"

#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace velocity::ui {

MediaBinWidget::MediaBinWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    listWidget_ = new QListWidget(this);
    listWidget_->setAlternatingRowColors(true);
    listWidget_->setToolTip("Double-click an asset to add it to the timeline");
    layout->addWidget(listWidget_);

    importButton_ = new QPushButton("Import Media", this);
    layout->addWidget(importButton_);

    connect(importButton_, &QPushButton::clicked, this, &MediaBinWidget::onImportClicked);
    connect(listWidget_, &QListWidget::itemDoubleClicked, this, &MediaBinWidget::onItemDoubleClicked);

    // Sync with existing clips on load
    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr& snapshot) {
        // Collect assets currently in use on the timeline to populate the list if empty
        if (listWidget_->count() == 0) {
            for (const auto& track : snapshot->tracks) {
                for (const auto& clip : track->clips) {
                    QString pathStr = QString::fromStdWString(clip->asset.wstring());
                    bool found = false;
                    for (int i = 0; i < listWidget_->count(); ++i) {
                        if (listWidget_->item(i)->data(Qt::UserRole).toString() == pathStr) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        QFileInfo info(pathStr);
                        auto* item = new QListWidgetItem(info.fileName(), listWidget_);
                        item->setData(Qt::UserRole, pathStr);
                        item->setToolTip(pathStr);
                    }
                }
            }
        }
    });
}

void MediaBinWidget::onImportClicked() {
    QString file = QFileDialog::getOpenFileName(
        this,
        "Import Media File",
        "",
        "Media Files (*.mp4 *.mkv *.mov *.mp3 *.wav);;All Files (*)"
    );

    if (file.isEmpty()) return;

    // Check if it's already in the bin
    bool alreadyExists = false;
    for (int i = 0; i < listWidget_->count(); ++i) {
        if (listWidget_->item(i)->data(Qt::UserRole).toString() == file) {
            alreadyExists = true;
            break;
        }
    }

    if (!alreadyExists) {
        QFileInfo info(file);
        auto* item = new QListWidgetItem(info.fileName(), listWidget_);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }

    // Automatically place it on the timeline for instant feedback
    std::filesystem::path stdPath = file.toStdWString();
    session_->importMedia(stdPath, 0); // target first track
}

void MediaBinWidget::onItemDoubleClicked() {
    QListWidgetItem* item = listWidget_->currentItem();
    if (!item) return;

    QString file = item->data(Qt::UserRole).toString();
    std::filesystem::path stdPath = file.toStdWString();
    
    // Add to timeline
    size_t targetTrack = session_->selectedTrackIdx().value_or(0);
    session_->importMedia(stdPath, targetTrack);
}

} // namespace velocity::ui
