#include "mainwindow.h"
#include "documentsession.h"
#include <spdlog/spdlog.h>
#include "../bin/media_bin_widget.h"
#include "../preview/previewwidget.h"
#include "../timeline/timeline_widget.h"
#include "../inspector/inspector_widget.h"
#include "../mixer/mixer_widget.h"
#include "../exportdlg/export_dialog.h"
#include "../playback/playback_controller.h"
#include "icons.h"
#include "theming.h"

#include <velocity/engine/project_io.h>

#include <QFileInfo>

#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QApplication>
#include <QFileDialog>

namespace velocity::ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Configure window basic properties
    resize(1440, 860);
    
    // 1. Initialize Document Session + real playback engine (audio-master clock)
    session_ = new DocumentSession(this);
    connect(session_, &DocumentSession::errorOccurred, this, &MainWindow::onShowError);
    playback_ = new PlaybackController(session_, this);

    updateTitle();

    // 2. Setup Docks and layout
    setupDocks();

    // 3. Setup Controls
    setupMenus();
    setupToolbar();
    setupStatusBar();

    // 4. Wire playback state into the UI
    mixer_->attachPlayback(playback_);
    connect(playback_, &PlaybackController::playStateChanged, this, [this](bool playing) {
        playPauseAction_->setText(playing ? "⏸ Pause" : "▶ Play");
    });

    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
        dirty_ = true;
        updateTitle();
    });
}

void MainWindow::updateTitle() {
    const QString name = projectPath_.isEmpty() ? "Untitled Project"
                                                : QFileInfo(projectPath_).completeBaseName();
    setWindowTitle(QString("%1%2 — Velocity").arg(name, dirty_ ? "*" : ""));
}

void MainWindow::setupMenus() {
    // Project Menu
    auto* projectMenu = menuBar()->addMenu("&Project");
    
    auto* newAct = projectMenu->addAction("&New Project", this, &MainWindow::onNewProject);
    newAct->setShortcut(QKeySequence::New);
    
    auto* openAct = projectMenu->addAction("&Open Project", this, &MainWindow::onOpenProject);
    openAct->setShortcut(QKeySequence::Open);

    auto* saveAct = projectMenu->addAction("&Save Project", this, &MainWindow::onSaveProject);
    saveAct->setShortcut(QKeySequence::Save);

    auto* saveAsAct = projectMenu->addAction("Save Project &As…", this, &MainWindow::onSaveProjectAs);
    saveAsAct->setShortcut(QKeySequence::SaveAs);

    projectMenu->addSeparator();
    
    auto* exitAct = projectMenu->addAction("E&xit", this, &QWidget::close);
    exitAct->setShortcut(QKeySequence::Quit);

    // Edit Menu
    auto* editMenu = menuBar()->addMenu("&Edit");

    auto* undoAct = editMenu->addAction(icons::icon("undo"), "&Undo", this,
                                        [this]() { session_->undo(); });
    undoAct->setShortcut(QKeySequence::Undo);

    auto* redoAct = editMenu->addAction(icons::icon("redo"), "&Redo", this,
                                        [this]() { session_->redo(); });
    redoAct->setShortcut(QKeySequence::Redo);

    editMenu->addSeparator();

    auto* splitAct = editMenu->addAction(icons::icon("split"), "&Split Clip", this,
                                         [this]() { session_->splitClipAtPlayhead(); });
    splitAct->setShortcut(QKeySequence(Qt::Key_S));

    auto* detachAct = editMenu->addAction(icons::icon("detach"), "De&tach Audio", this,
                                          [this]() { session_->detachAudioFromSelectedClip(); });
    detachAct->setShortcut(QKeySequence("Ctrl+Shift+D"));

    auto* deleteAct = editMenu->addAction(icons::icon("trash"), "&Delete Clip", this,
                                          [this]() { session_->deleteSelectedClip(); });
    deleteAct->setShortcut(QKeySequence(Qt::Key_Delete));

    // Timeline Menu
    auto* timelineMenu = menuBar()->addMenu("&Timeline");
    timelineMenu->addAction(icons::icon("plus"), "Add &Video Track", this,
                            [this]() { session_->addTrack(engine::TrackKind::video); });
    timelineMenu->addAction(icons::icon("plus"), "Add &Audio Track", this,
                            [this]() { session_->addTrack(engine::TrackKind::audio); });
    timelineMenu->addSeparator();
    timelineMenu->addAction(icons::icon("zoom-in"), "Zoom &In", this,
                            [this]() { timeline_->zoomIn(); });
    timelineMenu->addAction(icons::icon("zoom-out"), "Zoom &Out", this,
                            [this]() { timeline_->zoomOut(); });
    auto* fitAct = timelineMenu->addAction(icons::icon("zoom-fit"), "Zoom to &Fit", this,
                                           [this]() { timeline_->zoomToFit(); });
    fitAct->setShortcut(QKeySequence("Shift+Z"));
}

void MainWindow::setupToolbar() {
    auto* toolbar = addToolBar("Main Toolbar");
    toolbar->setObjectName("MainToolbar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto addTool = [&](const char* iconName, const QString& text, const QString& tip,
                       auto&& slot) {
        QAction* act = toolbar->addAction(icons::icon(iconName), text, this,
                                          std::forward<decltype(slot)>(slot));
        act->setToolTip(tip);
        return act;
    };

    addTool("import", "Import", "Import media files into the project",
            [this]() { mediaBin_->onImportClicked(); });
    addTool("export", "Export", "Export the timeline as MP4",
            [this]() { onExportVideo(); });

    toolbar->addSeparator();

    addTool("undo", "", "Undo (Ctrl+Z)", [this]() { session_->undo(); });
    addTool("redo", "", "Redo (Ctrl+Shift+Z)", [this]() { session_->redo(); });

    toolbar->addSeparator();

    addTool("split", "Split", "Split the clip at the playhead (S)",
            [this]() { session_->splitClipAtPlayhead(); });
    addTool("detach", "Detach Audio", "Separate audio from the selected video clip",
            [this]() { session_->detachAudioFromSelectedClip(); });
    addTool("trash", "", "Delete the selected clip (Del)",
            [this]() { session_->deleteSelectedClip(); });

    toolbar->addSeparator();

    addTool("zoom-in", "", "Zoom in (Ctrl+Wheel)", [this]() { timeline_->zoomIn(); });
    addTool("zoom-out", "", "Zoom out", [this]() { timeline_->zoomOut(); });
    addTool("zoom-fit", "", "Zoom to fit the whole timeline",
            [this]() { timeline_->zoomToFit(); });
}

void MainWindow::setupDocks() {
    // Set docking behaviors
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    // 1. Central Widget: Preview Window with playback buttons
    auto* centralWidget = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->setContentsMargins(4, 4, 4, 4);
    centralLayout->setSpacing(4);

    preview_ = new PreviewWidget(session_, centralWidget);
    centralLayout->addWidget(preview_);

    // Transport buttons layout
    auto* transportLayout = new QHBoxLayout();
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(8);

    auto makeTransportBtn = [&](const char* iconName, const QString& tip) {
        auto* btn = new QPushButton(centralWidget);
        btn->setIcon(icons::icon(iconName));
        btn->setToolTip(tip);
        btn->setFixedSize(34, 28);
        btn->setProperty("transport", true);
        return btn;
    };

    auto* toStartBtn = makeTransportBtn("skip-start", "Go to start");
    connect(toStartBtn, &QPushButton::clicked, this, [this]() { session_->setPlayhead(0); });
    transportLayout->addWidget(toStartBtn);

    auto* prevFrameBtn = makeTransportBtn("frame-prev", "Previous frame");
    connect(prevFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepBackward);
    transportLayout->addWidget(prevFrameBtn);

    playPauseAction_ = new QAction("Play", this);
    playPauseAction_->setShortcut(QKeySequence(Qt::Key_Space));
    addAction(playPauseAction_); // window-wide Space shortcut
    auto* playBtn = makeTransportBtn("play", "Play / pause (Space)");
    playBtn->setFixedSize(44, 28);
    connect(playBtn, &QPushButton::clicked, playPauseAction_, &QAction::trigger);
    connect(playPauseAction_, &QAction::changed, this, [playBtn, this]() {
        playBtn->setIcon(
            icons::icon(playPauseAction_->text().contains("Pause") ? "pause" : "play"));
    });
    connect(playPauseAction_, &QAction::triggered, this,
            [this]() { playback_->togglePlayPause(); });
    transportLayout->addWidget(playBtn);

    auto* stopBtn = makeTransportBtn("stop", "Stop (return to play start)");
    connect(stopBtn, &QPushButton::clicked, this, [this]() { playback_->stop(); });
    transportLayout->addWidget(stopBtn);

    loopAction_ = new QAction("Loop", this);
    loopAction_->setCheckable(true);
    auto* loopBtn = makeTransportBtn("loop", "Loop playback");
    loopBtn->setCheckable(true);
    connect(loopBtn, &QPushButton::toggled, this, [this](bool on) {
        loopAction_->setChecked(on);
        playback_->setLoop(on);
    });
    transportLayout->addWidget(loopBtn);

    auto* nextFrameBtn = makeTransportBtn("frame-next", "Next frame");
    connect(nextFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepForward);
    transportLayout->addWidget(nextFrameBtn);

    // Timecode readout
    auto* timecodeLabel = new QLabel("00:00.00", centralWidget);
    timecodeLabel->setStyleSheet("font-family: Consolas, monospace; font-size: 14px; "
                                 "font-weight: bold; color: #10b981; padding-left: 10px;");
    connect(session_, &DocumentSession::playheadChanged, this, [timecodeLabel](Tick t) {
        const double seconds = static_cast<double>(t) / kTickRate;
        const int mins = static_cast<int>(seconds) / 60;
        const int secs = static_cast<int>(seconds) % 60;
        const int hundredths = static_cast<int>((seconds - std::floor(seconds)) * 100);
        timecodeLabel->setText(QString::asprintf("%02d:%02d.%02d", mins, secs, hundredths));
    });
    transportLayout->addWidget(timecodeLabel);
    transportLayout->addStretch();

    centralLayout->addLayout(transportLayout);
    setCentralWidget(centralWidget);

    // 2. Left Dock: Media Bin
    auto* binDock = new QDockWidget("Media Catalog", this);
    binDock->setObjectName("MediaCatalogDock");
    binDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    mediaBin_ = new MediaBinWidget(session_, binDock);
    binDock->setWidget(mediaBin_);
    addDockWidget(Qt::LeftDockWidgetArea, binDock);

    // 3. Right Dock: Inspector
    auto* inspectorDock = new QDockWidget("Properties Inspector", this);
    inspectorDock->setObjectName("PropertiesInspectorDock");
    inspectorDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    inspector_ = new InspectorWidget(session_, inspectorDock);
    inspectorDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    // 4. Bottom Dock: Timeline
    auto* timelineDock = new QDockWidget("Sequence Timeline", this);
    timelineDock->setObjectName("SequenceTimelineDock");
    timelineDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    timeline_ = new TimelineWidget(session_, timelineDock);
    timelineDock->setWidget(timeline_);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);

    // 5. Bottom Right Dock: Audio Mixer
    auto* mixerDock = new QDockWidget("Audio Mixer", this);
    mixerDock->setObjectName("AudioMixerDock");
    mixerDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    mixer_ = new MixerWidget(session_, mixerDock);
    mixerDock->setWidget(mixer_);
    addDockWidget(Qt::BottomDockWidgetArea, mixerDock);

    // Split bottom dock area between Timeline and Mixer
    splitDockWidget(timelineDock, mixerDock, Qt::Horizontal);
}

void MainWindow::setupStatusBar() {
    auto* bar = statusBar();
    
    auto* fpsLabel = new QLabel("FPS: 30.00 |", this);
    bar->addPermanentWidget(fpsLabel);

    auto* resLabel = new QLabel("Format: 1920×1080 |", this);
    bar->addPermanentWidget(resLabel);

    auto* hardwareLabel = new QLabel("Engine: Direct3D 12 (DirectX Hardware Acceleration) |", this);
    bar->addPermanentWidget(hardwareLabel);

    auto* stateLabel = new QLabel("Project: Ready", this);
    bar->addWidget(stateLabel);
}

void MainWindow::onNewProject() {
    auto confirm = QMessageBox::question(this, "New Project", "Create a new project? All unsaved work will be lost.", QMessageBox::Yes | QMessageBox::No);
    if (confirm == QMessageBox::Yes) {
        // Re-instantiate session and playback engine
        playback_->pause();
        delete playback_;

        session_->deleteLater();
        session_ = new DocumentSession(this);
        connect(session_, &DocumentSession::errorOccurred, this, &MainWindow::onShowError);
        playback_ = new PlaybackController(session_, this);
        connect(playback_, &PlaybackController::playStateChanged, this, [this](bool playing) {
            playPauseAction_->setText(playing ? "⏸ Pause" : "▶ Play");
        });
        
        // Re-inject session pointers
        mediaBin_->deleteLater();
        inspector_->deleteLater();
        timeline_->deleteLater();
        mixer_->deleteLater();

        // Find dock widgets and replace
        QList<QDockWidget*> docks = findChildren<QDockWidget*>();
        for (auto* dock : docks) {
            if (dock->objectName() == "MediaCatalogDock") {
                mediaBin_ = new MediaBinWidget(session_, dock);
                dock->setWidget(mediaBin_);
            } else if (dock->objectName() == "PropertiesInspectorDock") {
                inspector_ = new InspectorWidget(session_, dock);
                dock->setWidget(inspector_);
            } else if (dock->objectName() == "SequenceTimelineDock") {
                timeline_ = new TimelineWidget(session_, dock);
                dock->setWidget(timeline_);
            } else if (dock->objectName() == "AudioMixerDock") {
                mixer_ = new MixerWidget(session_, dock);
                dock->setWidget(mixer_);
            }
        }
        mixer_->attachPlayback(playback_);
        
        // Update connections
        connect(session_, &DocumentSession::playheadChanged, this, [this](Tick) {
            // updates preview frame
        });
        connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
            updateTitle();
        });

        // Trigger updates
        session_->setPlayhead(0);
        projectPath_.clear();
        dirty_ = false;
        updateTitle();
        statusBar()->showMessage("New 1080p sequence created.", 5000);
    }
}

void MainWindow::onOpenProject() {
    if (dirty_) {
        const auto answer = QMessageBox::question(
            this, "Open Project", "Discard unsaved changes in the current project?",
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    const QString file = QFileDialog::getOpenFileName(this, "Open Velocity Project", QString(),
                                                      "Velocity Projects (*.velproj)");
    if (file.isEmpty())
        return;

    playback_->pause();
    auto loaded = engine::loadProject(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::critical(this, "Open Project",
                              QString::fromStdString(loaded.error()));
        return;
    }

    // Warn (but proceed) when referenced media is missing — clips stay
    // editable as offline placeholders (docs/03 §4).
    QStringList missing;
    for (const auto& track : (*loaded)->tracks)
        for (const auto& clip : track->clips)
            if (!std::filesystem::exists(clip->asset))
                missing << QFileInfo(QString::fromStdWString(clip->asset.wstring())).fileName();
    missing.removeDuplicates();
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, "Missing Media",
                             "These files were not found and will play as black/silence:\n" +
                                 missing.join('\n'));
    }

    session_->replaceDocument(std::move(loaded.value()));
    projectPath_ = file;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage("Project loaded: " + QFileInfo(file).fileName(), 5000);
}

bool MainWindow::saveTo(const QString& path) {
    auto result = engine::saveProject(session_->currentSnapshot(),
                                      std::filesystem::path(path.toStdWString()));
    if (!result) {
        QMessageBox::critical(this, "Save Project", QString::fromStdString(result.error()));
        return false;
    }
    projectPath_ = path;
    dirty_ = false;
    updateTitle();
    statusBar()->showMessage("Project saved: " + QFileInfo(path).fileName(), 5000);
    return true;
}

void MainWindow::onSaveProject() {
    if (projectPath_.isEmpty()) {
        onSaveProjectAs();
        return;
    }
    saveTo(projectPath_);
}

void MainWindow::onSaveProjectAs() {
    QString file = QFileDialog::getSaveFileName(this, "Save Velocity Project",
                                                projectPath_.isEmpty() ? "untitled.velproj"
                                                                       : projectPath_,
                                                "Velocity Projects (*.velproj)");
    if (file.isEmpty())
        return;
    if (!file.endsWith(".velproj", Qt::CaseInsensitive))
        file += ".velproj";
    saveTo(file);
}

void MainWindow::onExportVideo() {
    auto seq = session_->currentSnapshot();
    if (seq->duration() == 0) {
        QMessageBox::warning(this, "Export", "The timeline is empty. Import media before exporting.");
        return;
    }

    ExportDialog dialog(seq, playback_->masterGain(), this);
    dialog.exec();
}

void MainWindow::onStepForward() {
    playback_->pause();
    const Rational fps = session_->currentSnapshot()->frameRate;
    const auto frame = frameIndexFromTicks(session_->playhead(), fps);
    session_->setPlayhead(ticksFromFrameIndex(frame + 1, fps));
}

void MainWindow::onStepBackward() {
    playback_->pause();
    const Rational fps = session_->currentSnapshot()->frameRate;
    const auto frame = frameIndexFromTicks(session_->playhead(), fps);
    if (frame > 0)
        session_->setPlayhead(ticksFromFrameIndex(frame - 1, fps));
    else
        session_->setPlayhead(0);
}

void MainWindow::onShowError(const QString& msg) {
    statusBar()->showMessage("Error: " + msg, 5000);
}

} // namespace velocity::ui
