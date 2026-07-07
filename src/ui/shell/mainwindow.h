#pragma once

#include <QMainWindow>
#include <memory>

class QAction;
class QToolBar;

namespace velocity::ui {

class DocumentSession;
class MediaBinWidget;
class PreviewWidget;
class TimelineWidget;
class InspectorWidget;
class MixerWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onExportVideo();
    void onPlayPauseToggled();
    void onStepForward();
    void onStepBackward();
    void onShowError(const QString& msg);

private:
    void setupMenus();
    void setupToolbar();
    void setupDocks();
    void setupStatusBar();
    void updateTitle();

    // Session State
    DocumentSession* session_;

    // Widgets
    MediaBinWidget* mediaBin_;
    PreviewWidget* preview_;
    TimelineWidget* timeline_;
    InspectorWidget* inspector_;
    MixerWidget* mixer_;

    // Actions
    QAction* playPauseAction_;
    QTimer* playbackTimer_;
    bool isPlaying_ = false;
};

} // namespace velocity::ui
