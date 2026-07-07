#pragma once

#include <QWidget>

class QListWidget;
class QPushButton;

namespace velocity::ui {

class DocumentSession;

class MediaBinWidget : public QWidget {
    Q_OBJECT

public:
    explicit MediaBinWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~MediaBinWidget() override = default;

public slots:
    void onImportClicked();
    void onItemDoubleClicked();

private:
    DocumentSession* session_;
    QListWidget* listWidget_;
    QPushButton* importButton_;
};

} // namespace velocity::ui
