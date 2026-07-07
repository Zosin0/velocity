// Velocity entry point: verify the FFmpeg runtime, then launch the Qt shell.

#include <velocity/foundation/log.h>
#include <velocity/media/ffmpeg_info.h>

#include <ui/shell/mainwindow.h>
#include <ui/shell/theming.h>

#include <QApplication>
#include <spdlog/spdlog.h>

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

    spdlog::info("entering event loop");
    const int code = app.exec();
    spdlog::info("clean exit with code {}", code);
    return code;
}
