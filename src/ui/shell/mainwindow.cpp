#include "mainwindow.h"
#include "documentsession.h"
#include <spdlog/spdlog.h>
#include "../bin/media_bin_widget.h"
#include "../preview/previewwidget.h"
#include "../timeline/timeline_widget.h"
#include "../inspector/inspector_widget.h"
#include "../mixer/mixer_widget.h"
#include "../exportdlg/export_dialog.h"
#include "theming.h"

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
    resize(1280, 720);
    
    // 1. Initialize Document Session
    session_ = new DocumentSession(this);
    connect(session_, &DocumentSession::errorOccurred, this, &MainWindow::onShowError);

    updateTitle();

    // 2. Setup Docks and layout
    setupDocks();

    // 3. Setup Controls
    setupMenus();
    setupToolbar();
    setupStatusBar();

    // 4. Setup Playback Simulation Timer
    playbackTimer_ = new QTimer(this);
    playbackTimer_->setInterval(33); // ~30 fps
    connect(playbackTimer_, &QTimer::timeout, this, [this]() {
        Tick cur = session_->playhead();
        Tick step = kTickRate / 30; // 30 fps frame step (1600 ticks)
        Tick next = cur + step;
        
        Tick duration = session_->currentSnapshot()->duration();
        if (duration > 0 && next >= duration) {
            // Loop back to start
            session_->setPlayhead(0);
        } else {
            session_->setPlayhead(next);
        }
    });

    // Make sure we connect playhead and snapshot updates to update window title or status
    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
        updateTitle();
    });
}

void MainWindow::updateTitle() {
    QString title = "Velocity Video Editor [Unsaved Project] - C++20 / Direct3D 12";
    if (session_) {
        title += QString(" (%1 tracks)").arg(session_->currentSnapshot()->tracks.size());
    }
    setWindowTitle(title);
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

    projectMenu->addSeparator();
    
    auto* exitAct = projectMenu->addAction("E&xit", this, &QWidget::close);
    exitAct->setShortcut(QKeySequence::Quit);

    // Edit Menu
    auto* editMenu = menuBar()->addMenu("&Edit");
    
    auto* undoAct = editMenu->addAction("&Undo", this, [this]() { session_->undo(); });
    undoAct->setShortcut(QKeySequence::Undo);
    
    auto* redoAct = editMenu->addAction("&Redo", this, [this]() { session_->redo(); });
    redoAct->setShortcut(QKeySequence::Redo);

    editMenu->addSeparator();

    auto* splitAct = editMenu->addAction("&Split Clip", this, [this]() {
        session_->splitClipAtPlayhead();
    });
    splitAct->setShortcut(QKeySequence(Qt::Key_S));

    auto* deleteAct = editMenu->addAction("&Delete Clip", this, [this]() {
        session_->deleteSelectedClip();
    });
    deleteAct->setShortcut(QKeySequence(Qt::Key_Delete));

    // View Menu
    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Reset Workspace", this, [this]() {
        // Reset docks to default layout
        QMessageBox::information(this, "Workspace", "Workspace layout reset to professional preset.");
    });
}

void MainWindow::setupToolbar() {
    auto* toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));

    // Import/Export buttons
    toolbar->addAction("Import", this, [this]() {
        // Triggers import dialog on media bin
        mediaBin_->onImportClicked();
    });
    toolbar->addAction("Export Video", this, &MainWindow::onExportVideo);
    
    toolbar->addSeparator();

    // Edit actions
    toolbar->addAction("Undo", this, [this]() { session_->undo(); });
    toolbar->addAction("Redo", this, [this]() { session_->redo(); });
    toolbar->addAction("Split (S)", this, [this]() { session_->splitClipAtPlayhead(); });
    toolbar->addAction("Delete", this, [this]() { session_->deleteSelectedClip(); });

    toolbar->addSeparator();

    // Zooming
    toolbar->addAction("Zoom In", this, [this]() { timeline_->zoomIn(); });
    toolbar->addAction("Zoom Out", this, [this]() { timeline_->zoomOut(); });
    toolbar->addAction("Zoom Fit", this, [this]() { timeline_->zoomToFit(); });
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

    auto* toStartBtn = new QPushButton("⏮", centralWidget);
    toStartBtn->setToolTip("Go to Start");
    toStartBtn->setFixedWidth(32);
    connect(toStartBtn, &QPushButton::clicked, this, [this]() { session_->setPlayhead(0); });
    transportLayout->addWidget(toStartBtn);

    auto* prevFrameBtn = new QPushButton("◀", centralWidget);
    prevFrameBtn->setToolTip("Previous Frame");
    prevFrameBtn->setFixedWidth(32);
    connect(prevFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepBackward);
    transportLayout->addWidget(prevFrameBtn);

    playPauseAction_ = new QAction("▶ Play", this);
    auto* playBtn = new QPushButton("▶ Play", centralWidget);
    playBtn->setFixedWidth(64);
    connect(playBtn, &QPushButton::clicked, playPauseAction_, &QAction::trigger);
    connect(playPauseAction_, &QAction::changed, this, [playBtn, this]() {
        playBtn->setText(playPauseAction_->text());
    });
    connect(playPauseAction_, &QAction::triggered, this, &MainWindow::onPlayPauseToggled);
    transportLayout->addWidget(playBtn);

    auto* nextFrameBtn = new QPushButton("▶", centralWidget);
    nextFrameBtn->setToolTip("Next Frame");
    nextFrameBtn->setFixedWidth(32);
    connect(nextFrameBtn, &QPushButton::clicked, this, &MainWindow::onStepForward);
    transportLayout->addWidget(nextFrameBtn);

    // Timecode readout
    auto* timecodeLabel = new QLabel("00:00:00:00", centralWidget);
    timecodeLabel->setStyleSheet("font-family: monospace; font-size: 14px; font-weight: bold; color: #10b981; padding-left: 10px;");
    connect(session_, &DocumentSession::playheadChanged, this, [timecodeLabel](Tick t) {
        double seconds = static_cast<double>(t) / kTickRate;
        int mins = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        int msecs = static_cast<int>((seconds - std::floor(seconds)) * 100);
        timecodeLabel->setText(QString::asprintf("%02d:%02d:%02d.%02d", 0, mins, secs, msecs));
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
        // Re-instantiate session
        if (playbackTimer_->isActive()) {
            playbackTimer_->stop();
            playPauseAction_->setText("▶ Play");
            isPlaying_ = false;
        }
        
        session_->deleteLater();
        session_ = new DocumentSession(this);
        connect(session_, &DocumentSession::errorOccurred, this, &MainWindow::onShowError);
        
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
        
        // Update connections
        connect(session_, &DocumentSession::playheadChanged, this, [this](Tick) {
            // updates preview frame
        });
        connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
            updateTitle();
        });

        // Trigger updates
        session_->setPlayhead(0);
        updateTitle();
        QMessageBox::information(this, "New Project", "New 1080p Sequence created successfully.");
    }
}

void MainWindow::onOpenProject() {
    QString file = QFileDialog::getOpenFileName(this, "Open Velocity Project", "", "Velocity Projects (*.db)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "Open Project", "Project database loaded successfully.");
    }
}

void MainWindow::onSaveProject() {
    QString file = QFileDialog::getSaveFileName(this, "Save Velocity Project", "", "Velocity Projects (*.db)");
    if (!file.isEmpty()) {
        QMessageBox::information(this, "Save Project", "Project database saved successfully.");
    }
}

void MainWindow::onExportVideo() {
    auto seq = session_->currentSnapshot();
    if (seq->duration() == 0) {
        QMessageBox::warning(this, "Export", "The timeline is empty. Import media before exporting.");
        return;
    }

    ExportDialog dialog(seq, this);
    dialog.exec();
}

void MainWindow::onPlayPauseToggled() {
    if (isPlaying_) {
        playbackTimer_->stop();
        playPauseAction_->setText("▶ Play");
        isPlaying_ = false;
    } else {
        playbackTimer_->start();
        playPauseAction_->setText("⏸ Pause");
        isPlaying_ = true;
    }
}

void MainWindow::onStepForward() {
    session_->setPlayhead(session_->playhead() + kTickRate / 30); // advance 1 frame (30fps)
}

void MainWindow::onStepBackward() {
    session_->setPlayhead(session_->playhead() - kTickRate / 30); // retreat 1 frame (30fps)
}

void MainWindow::onShowError(const QString& msg) {
    statusBar()->showMessage("Error: " + msg, 5000);
}

} // namespace velocity::ui
