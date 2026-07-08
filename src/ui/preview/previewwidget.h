#pragma once

#include <velocity/gpu/device.h>
#include <velocity/gpu/swapchain.h>

#include <QWidget>
#include <filesystem>
#include <map>
#include <memory>
#include <QImage>

namespace velocity::media {
class SequentialFrameReader;
}

namespace velocity::ui {

class DocumentSession;
class VideoSurfaceWidget;

class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(DocumentSession* session, QWidget* parent = nullptr);
    ~PreviewWidget() override;

    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void renderFrame();

    DocumentSession* session_;
    std::unique_ptr<gpu::Device> device_;
    std::unique_ptr<gpu::Swapchain> swapchain_;
    float currentClearColor_[4] = {0.07f, 0.07f, 0.07f, 1.0f};

    // Video preview components
    VideoSurfaceWidget* videoSurface_ = nullptr;
    std::map<std::filesystem::path, std::unique_ptr<media::SequentialFrameReader>> readers_;
};

} // namespace velocity::ui
