#pragma once

#include <QListWidget>
#include <QWidget>

class QPushButton;
class QLineEdit;

namespace velocity::ui {

class DocumentSession;

// List widget whose outgoing drags carry file URLs, so the timeline handles
// bin drags and Explorer drags through one code path.
class MediaListWidget : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
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

private:
    void addAsset(const QString& filePath);
    void applyFilter(const QString& text);

    DocumentSession* session_;
    MediaListWidget* listWidget_;
    QPushButton* importButton_;
    QLineEdit* searchEdit_;
};

} // namespace velocity::ui
