#include "media_bin_widget.h"
#include "../services/thumbnail_cache.h"
#include "../shell/documentsession.h"
#include "../shell/icons.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHelpEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace velocity::ui {

namespace {
const char* kImportFilter =
    "Media Files (*.mp4 *.mov *.mkv *.webm *.avi *.m4v "
    "*.mp3 *.wav *.flac *.aac *.ogg "
    "*.png *.jpg *.jpeg *.webp *.bmp *.svg);;All Files (*)";

constexpr int kCardWidth = 148;
constexpr int kThumbHeight = 84;
constexpr int kCardHeight = kThumbHeight + 40;

QString formatDuration(double seconds) {
    if (seconds <= 0.0)
        return {};
    const int total = static_cast<int>(seconds + 0.5);
    return QString::asprintf("%d:%02d", total / 60, total % 60);
}
} // namespace

QMimeData* MediaListWidget::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    for (const auto* item : items)
        urls << QUrl::fromLocalFile(item->data(Qt::UserRole).toString());
    mime->setUrls(urls);
    return mime;
}

// ------------------------------------------------------------ card delegate

MediaCardDelegate::MediaCardDelegate(ThumbnailCache* thumbs, QObject* parent)
    : QStyledItemDelegate(parent), thumbs_(thumbs) {}

void MediaCardDelegate::setHover(const QModelIndex& index, int frame) {
    hoverIndex_ = index;
    hoverFrame_ = frame;
}

QSize MediaCardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return QSize(kCardWidth, kCardHeight);
}

void MediaCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect card = option.rect.adjusted(3, 3, -3, -3);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // Card body.
    QPainterPath body;
    body.addRoundedRect(card, 6, 6);
    painter->fillPath(body, QColor(hovered ? 0x26 : 0x1f, hovered ? 0x26 : 0x1f,
                                   hovered ? 0x2c : 0x24));
    if (selected) {
        painter->setPen(QPen(QColor(0x4d, 0x8b, 0xf7), 1.6));
        painter->drawPath(body);
    }

    const QString filePath = index.data(Qt::UserRole).toString();
    const MediaCardInfo* info = thumbs_->infoFor(filePath);

    // Thumbnail area.
    const QRect thumbRect(card.x(), card.y(), card.width(), kThumbHeight);
    QPainterPath thumbClip;
    thumbClip.addRoundedRect(thumbRect, 6, 6);
    painter->setClipPath(thumbClip);
    painter->fillRect(thumbRect, QColor(0x13, 0x13, 0x16));

    QImage shown;
    if (info) {
        // Hover scrub: cycle the preview strip while the cursor rests here.
        if (hoverIndex_.isValid() && hoverIndex_ == index && !info->preview.isEmpty())
            shown = info->preview[hoverFrame_ % info->preview.size()];
        else
            shown = info->thumbnail;
    }
    if (!shown.isNull()) {
        const QSize fit = shown.size().scaled(thumbRect.size(), Qt::KeepAspectRatioByExpanding);
        const QRect target(thumbRect.center().x() - fit.width() / 2,
                           thumbRect.center().y() - fit.height() / 2, fit.width(),
                           fit.height());
        painter->drawImage(target, shown);
    } else {
        // Kind icon placeholder (audio, or still generating).
        QString iconName = "film";
        if (info && !info->hasVideo)
            iconName = "music";
        icons::icon(iconName, QColor(0x5c, 0x5c, 0x66))
            .paint(painter, QRect(thumbRect.center().x() - 14, thumbRect.center().y() - 14,
                                  28, 28));
        if (!info) {
            painter->setPen(QColor(0x6a, 0x6a, 0x72));
            painter->setFont(QFont("Segoe UI", 7));
            painter->drawText(thumbRect.adjusted(0, 0, 0, -6),
                              Qt::AlignBottom | Qt::AlignHCenter, "analyzing…");
        }
    }

    // Duration badge.
    if (info && info->durationSeconds > 0.0) {
        const QString dur = formatDuration(info->durationSeconds);
        painter->setFont(QFont("Segoe UI", 7, QFont::DemiBold));
        const QRect badge(thumbRect.right() - 38, thumbRect.bottom() - 18, 34, 14);
        QPainterPath badgePath;
        badgePath.addRoundedRect(badge, 3, 3);
        painter->fillPath(badgePath, QColor(0, 0, 0, 170));
        painter->setPen(QColor(0xe8, 0xe8, 0xec));
        painter->drawText(badge, Qt::AlignCenter, dur);
    }
    painter->setClipping(false);

    // Name + facts.
    const QRect nameRect(card.x() + 7, thumbRect.bottom() + 3, card.width() - 14, 16);
    painter->setPen(QColor(0xd6, 0xd6, 0xdc));
    painter->setFont(QFont("Segoe UI", 8, QFont::DemiBold));
    const QString name = QFileInfo(filePath).fileName();
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(name, Qt::ElideMiddle,
                                                        nameRect.width()));

    painter->setPen(QColor(0x8a, 0x8a, 0x92));
    painter->setFont(QFont("Segoe UI", 7));
    const QRect factsRect(card.x() + 7, nameRect.bottom(), card.width() - 14, 14);
    const QString facts = info ? info->details : QString();
    painter->drawText(factsRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(facts, Qt::ElideRight,
                                                        factsRect.width()));

    painter->restore();
}

// ------------------------------------------------------------- bin widget

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

    thumbs_ = new ThumbnailCache(this);

    listWidget_ = new MediaListWidget(this);
    listWidget_->setViewMode(QListView::IconMode);
    listWidget_->setResizeMode(QListView::Adjust);
    listWidget_->setMovement(QListView::Static);
    listWidget_->setSpacing(2);
    listWidget_->setUniformItemSizes(true);
    listWidget_->setDragEnabled(true);
    listWidget_->setDefaultDropAction(Qt::CopyAction);
    listWidget_->setMouseTracking(true);
    listWidget_->viewport()->installEventFilter(this);
    listWidget_->setStyleSheet("QListWidget { border: none; background: transparent; }");
    listWidget_->setToolTip(
        "Drag an asset onto the timeline, or double-click to append it to a track");

    delegate_ = new MediaCardDelegate(thumbs_, this);
    listWidget_->setItemDelegate(delegate_);
    layout->addWidget(listWidget_);

    connect(thumbs_, &ThumbnailCache::cardReady, this,
            [this](const QString&) { listWidget_->viewport()->update(); });

    // Hover preview: advance one frame every 200 ms (~2 s over 10 frames).
    hoverTimer_ = new QTimer(this);
    hoverTimer_->setInterval(200);
    connect(hoverTimer_, &QTimer::timeout, this, [this]() {
        static int frame = 0;
        const QModelIndex idx = listWidget_->indexAt(
            listWidget_->viewport()->mapFromGlobal(QCursor::pos()));
        if (idx.isValid()) {
            delegate_->setHover(idx, ++frame);
            listWidget_->viewport()->update();
        } else {
            delegate_->setHover(QModelIndex(), 0);
            hoverTimer_->stop();
            listWidget_->viewport()->update();
        }
    });

    importButton_ = new QPushButton(icons::icon("import"), " Import Media", this);
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

bool MediaBinWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == listWidget_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            if (!hoverTimer_->isActive())
                hoverTimer_->start();
        } else if (event->type() == QEvent::Leave) {
            delegate_->setHover(QModelIndex(), 0);
            hoverTimer_->stop();
            listWidget_->viewport()->update();
        } else if (event->type() == QEvent::Paint && listWidget_->count() == 0) {
            // Empty state.
            QPainter p(listWidget_->viewport());
            p.setPen(QColor(0x6a, 0x6a, 0x72));
            p.setFont(QFont("Segoe UI", 9));
            icons::icon("import", QColor(0x4a, 0x4a, 0x52))
                .paint(&p, QRect(listWidget_->viewport()->width() / 2 - 16,
                                 listWidget_->viewport()->height() / 2 - 40, 32, 32));
            p.drawText(listWidget_->viewport()->rect().adjusted(12, 0, -12, 0),
                       Qt::AlignCenter | Qt::TextWordWrap,
                       "No media yet.\nImport files or drop them here from Explorer.");
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MediaBinWidget::addAsset(const QString& filePath) {
    if (filePath.isEmpty())
        return;
    for (int i = 0; i < listWidget_->count(); ++i)
        if (listWidget_->item(i)->data(Qt::UserRole).toString() == filePath)
            return;

    auto* item = new QListWidgetItem(listWidget_);
    item->setData(Qt::UserRole, filePath);
    item->setData(Qt::DisplayRole, QFileInfo(filePath).fileName());
    item->setToolTip(filePath);
    thumbs_->infoFor(filePath); // kick off background generation immediately
    applyFilter(searchEdit_->text());
}

void MediaBinWidget::applyFilter(const QString& text) {
    for (int i = 0; i < listWidget_->count(); ++i) {
        auto* item = listWidget_->item(i);
        const QString name = QFileInfo(item->data(Qt::UserRole).toString()).fileName();
        item->setHidden(!text.isEmpty() && !name.contains(text, Qt::CaseInsensitive));
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
