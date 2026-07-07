#pragma once

#include <QWidget>

class QSlider;
class QProgressBar;
class QLabel;
class QTimer;

namespace velocity::ui {

class DocumentSession;

class MixerWidget : public QWidget {
    Q_OBJECT

public:
    explicit MixerWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~MixerWidget() override = default;

private slots:
    void updateMeters();

private:
    DocumentSession* session_;
    QSlider* masterSlider_;
    QProgressBar* leftMeter_;
    QProgressBar* rightMeter_;
    QLabel* dbLabel_;
    QTimer* meterTimer_;
};

} // namespace velocity::ui
