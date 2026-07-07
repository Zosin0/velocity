#include "theming.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace velocity::ui::theming {

void applyDarkTheme(QApplication& app) {
    // Enable Fusion style as our base flat appearance
    app.setStyle(QStyleFactory::create("Fusion"));

    // Set custom QPalette colors for robust system overrides
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(24, 24, 24));
    darkPalette.setColor(QPalette::WindowText, QColor(227, 227, 227));
    darkPalette.setColor(QPalette::Base, QColor(15, 15, 15));
    darkPalette.setColor(QPalette::AlternateBase, QColor(24, 24, 24));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(15, 15, 15));
    darkPalette.setColor(QPalette::ToolTipText, QColor(227, 227, 227));
    darkPalette.setColor(QPalette::Text, QColor(227, 227, 227));
    darkPalette.setColor(QPalette::Button, QColor(38, 38, 38));
    darkPalette.setColor(QPalette::ButtonText, QColor(227, 227, 227));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(59, 130, 246));
    darkPalette.setColor(QPalette::Highlight, QColor(59, 130, 246));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);

    // Apply global stylesheet (QSS) for custom widgets
    app.setStyleSheet(R"(
        /* Global Defaults */
        QWidget {
            background-color: #181818;
            color: #e3e3e3;
            font-family: 'Segoe UI', 'Inter', Helvetica, sans-serif;
            font-size: 12px;
            border: none;
        }

        /* MainWindow */
        QMainWindow {
            background-color: #121212;
        }

        /* QDockWidget styling */
        QDockWidget {
            color: #b0b0b0;
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
            background-color: #1e1e1e;
            border: 1px solid #2a2a2a;
        }
        QDockWidget::title {
            background-color: #222222;
            text-align: left;
            padding-left: 8px;
            padding-top: 4px;
            padding-bottom: 4px;
            border-bottom: 1px solid #2a2a2a;
            font-weight: bold;
        }

        /* Menu Bar */
        QMenuBar {
            background-color: #181818;
            border-bottom: 1px solid #2a2a2a;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 10px;
        }
        QMenuBar::item:selected {
            background-color: #2a2a2a;
            border-radius: 4px;
        }

        /* Menu */
        QMenu {
            background-color: #1e1e1e;
            border: 1px solid #2a2a2a;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 20px;
            border-radius: 2px;
        }
        QMenu::item:selected {
            background-color: #3b82f6;
            color: white;
        }

        /* Toolbar */
        QToolBar {
            background-color: #181818;
            border-bottom: 1px solid #2a2a2a;
            spacing: 6px;
            padding: 4px;
        }
        QToolButton {
            background-color: #262626;
            border: 1px solid #333333;
            border-radius: 4px;
            padding: 4px 8px;
            min-height: 18px;
        }
        QToolButton:hover {
            background-color: #333333;
            border-color: #444444;
        }
        QToolButton:pressed {
            background-color: #3b82f6;
            border-color: #3b82f6;
            color: white;
        }

        /* Buttons */
        QPushButton {
            background-color: #2a2a2a;
            border: 1px solid #3a3a3a;
            border-radius: 4px;
            padding: 5px 12px;
            color: #e3e3e3;
        }
        QPushButton:hover {
            background-color: #333333;
            border-color: #4a4a4a;
        }
        QPushButton:pressed {
            background-color: #3b82f6;
            border-color: #3b82f6;
            color: white;
        }
        QPushButton:disabled {
            background-color: #1a1a1a;
            border-color: #222222;
            color: #666666;
        }

        /* Text Input & Spinboxes */
        QLineEdit, QTextEdit, QSpinBox, QDoubleSpinBox {
            background-color: #0f0f0f;
            border: 1px solid #2a2a2a;
            border-radius: 4px;
            padding: 4px;
            color: #ffffff;
            selection-background-color: #3b82f6;
        }
        QLineEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #3b82f6;
        }
        QSpinBox::up-button, QDoubleSpinBox::up-button,
        QSpinBox::down-button, QDoubleSpinBox::down-button {
            background-color: #222222;
            border: none;
            width: 16px;
        }
        QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
        QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
            background-color: #333333;
        }

        /* Tabs */
        QTabWidget::pane {
            border: 1px solid #2a2a2a;
            background-color: #1e1e1e;
        }
        QTabBar::tab {
            background-color: #181818;
            color: #888888;
            border: 1px solid #2a2a2a;
            padding: 6px 12px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background-color: #1e1e1e;
            color: #e3e3e3;
            border-bottom-color: #1e1e1e;
        }
        QTabBar::tab:hover:!selected {
            background-color: #222222;
            color: #b0b0b0;
        }

        /* Scrollbars */
        QScrollBar:vertical {
            background-color: #121212;
            width: 12px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background-color: #2a2a2a;
            min-height: 20px;
            border-radius: 6px;
            margin: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #444444;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background-color: #121212;
            height: 12px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal {
            background-color: #2a2a2a;
            min-width: 20px;
            border-radius: 6px;
            margin: 2px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: #444444;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }

        /* Status Bar */
        QStatusBar {
            background-color: #181818;
            border-top: 1px solid #2a2a2a;
            font-size: 11px;
            color: #888888;
        }
        QStatusBar QLabel {
            color: #a0a0a0;
            padding-right: 10px;
        }

        /* List View */
        QListView, QTreeView, QListWidget {
            background-color: #121212;
            border: 1px solid #2a2a2a;
            border-radius: 4px;
            padding: 4px;
        }
        QListView::item:hover, QListWidget::item:hover {
            background-color: #222222;
            border-radius: 2px;
        }
        QListView::item:selected, QListWidget::item:selected {
            background-color: #3b82f6;
            color: white;
            border-radius: 2px;
        }

        /* Volume Meter background overlay */
        QProgressBar {
            background-color: #121212;
            border: 1px solid #2a2a2a;
            border-radius: 3px;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #10b981; /* emerald/green default */
            border-radius: 2px;
        }
    )");
}

} // namespace velocity::ui::theming
