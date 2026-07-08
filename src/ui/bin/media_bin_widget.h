#pragma once

#include <QListWidget>
#include <QStyledItemDelegate>
#include <QWidget>

class QPushButton;
class QLineEdit;
class QTimer;

namespace velocity::ui {

class DocumentSession;
class ThumbnailCache;

// List widget whose outgoing drags carry file URLs, so the timeline handles
// bin drags and Explorer drags through one code path.
class MediaListWidget : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// Card renderer: thumbnail (or kind icon), name, duration badge, facts line.
// While the cursor rests on a video card, an ~2 s frame strip cycles as an
// animated preview (frames generated asynchronously by ThumbnailCache).
class MediaCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    MediaCardDelegate(ThumbnailCache* thumbs, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setHover(const QModelIndex& index, int frame);
    [[nodiscard]] QPersistentModelIndex hoverIndex() const { return hoverIndex_; }

private:
    ThumbnailCache* thumbs_;
    QPersistentModelIndex hoverIndex_;
    int hoverFrame_ = 0;
};

class MediaBinWidget : public QWidget {
    Q_OBJECT

public:
    explicit MediaBinWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~MediaBinWidget() override = default;

public slots:
    void onImportClicked();
    void onItemDoubleClicked();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void addAsset(const QString& filePath);
    void applyFilter(const QString& text);

    DocumentSession* session_;
    MediaListWidget* listWidget_;
    QPushButton* importButton_;
    QLineEdit* searchEdit_;
    ThumbnailCache* thumbs_;
    MediaCardDelegate* delegate_;
    QTimer* hoverTimer_;
};

} // namespace velocity::ui
