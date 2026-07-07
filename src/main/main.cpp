// Velocity entry point: verify the FFmpeg runtime, then launch the Qt shell.

#include <velocity/foundation/log.h>
#include <velocity/media/ffmpeg_info.h>

#include <ui/shell/mainwindow.h>
#include <ui/shell/theming.h>

#include <QApplication>
#include <QTimer>
#include <spdlog/spdlog.h>

#include <cstring>

int main(int argc, char* argv[]) {
    velocity::log::init("velocity");
    spdlog::info("Velocity 0.1.0 starting");

    const auto v = velocity::media::runtimeVersions();
    spdlog::info("FFmpeg runtime: avutil {}.{} avcodec {}.{} avformat {}.{} ({})",
                 v.avutil >> 16, (v.avutil >> 8) & 0xff, v.avcodec >> 16, (v.avcodec >> 8) & 0xff,
                 v.avformat >> 16, (v.avformat >> 8) & 0xff, v.license);

    if (!velocity::media::runtimeMatchesHeaders()) {
        spdlog::error("FFmpeg runtime/header version mismatch");
        return 1;
    }

    QApplication app(argc, argv);
    velocity::ui::theming::applyDarkTheme(app);

    velocity::ui::MainWindow win;
    win.show();

    // CI launch check: bring the full UI up, spin the event loop briefly,
    // then exit cleanly. A crash during init fails the step; a clean start
    // exits 0 instead of blocking the runner forever.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) {
            spdlog::info("smoke mode: exiting after event-loop warmup");
            QTimer::singleShot(3000, &app, &QCoreApplication::quit);
            break;
        }
    }

    spdlog::info("entering event loop");
    const int code = app.exec();
    spdlog::info("clean exit with code {}", code);
    return code;
}
