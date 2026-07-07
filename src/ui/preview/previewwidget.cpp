#include "previewwidget.h"
#include "../shell/documentsession.h"

#include <velocity/foundation/log.h>
#include <velocity/engine/compile.h>
#include <velocity/media/sequential_reader.h>
#include <spdlog/spdlog.h>
#include <QResizeEvent>
#include <QPainter>
#include <QColor>
#include <QSize>
#include <QRect>
#include <algorithm>
#include <vector>

namespace velocity::ui {

// Utility to convert VideoFrame to QImage
static QImage convertVideoFrameToQImage(const velocity::media::VideoFrame& frame) {
    auto cpuFrameRes = frame.transferToCpu();
    if (!cpuFrameRes) {
        return QImage();
    }
    const auto& cpuFrame = cpuFrameRes.value();

    int w = cpuFrame.width();
    int h = cpuFrame.height();
    int fmt = cpuFrame.pixelFormatInt();

    QImage img(w, h, QImage::Format_RGB32);

    if (fmt == 0) { // AV_PIX_FMT_YUV420P
        const std::uint8_t* yData = cpuFrame.data(0);
        const std::uint8_t* uData = cpuFrame.data(1);
        const std::uint8_t* vData = cpuFrame.data(2);
        int yStride = cpuFrame.stride(0);
        int uStride = cpuFrame.stride(1);
        int vStride = cpuFrame.stride(2);

        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int yVal = yData[y * yStride + x];
                int uVal = uData[(y / 2) * uStride + (x / 2)] - 128;
                int vVal = vData[(y / 2) * vStride + (x / 2)] - 128;

                int r = std::clamp(static_cast<int>(yVal + 1.402 * vVal), 0, 255);
                int g = std::clamp(static_cast<int>(yVal - 0.344136 * uVal - 0.714136 * vVal), 0, 255);
                int b = std::clamp(static_cast<int>(yVal + 1.772 * uVal), 0, 255);

                line[x] = qRgb(r, g, b);
            }
        }
    } else if (fmt == 23 || fmt == 24) { // AV_PIX_FMT_NV12 (23) or AV_PIX_FMT_NV21 (24)
        const std::uint8_t* yData = cpuFrame.data(0);
        const std::uint8_t* uvData = cpuFrame.data(1);
        int yStride = cpuFrame.stride(0);
        int uvStride = cpuFrame.stride(1);

        bool isNV12 = (fmt == 23);

        for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                int yVal = yData[y * yStride + x];
                
                int uvIdx = (y / 2) * uvStride + (x / 2) * 2;
                int uVal = uvData[uvIdx + (isNV12 ? 0 : 1)] - 128;
                int vVal = uvData[uvIdx + (isNV12 ? 1 : 0)] - 128;

                int r = std::clamp(static_cast<int>(yVal + 1.402 * vVal), 0, 255);
                int g = std::clamp(static_cast<int>(yVal - 0.344136 * uVal - 0.714136 * vVal), 0, 255);
                int b = std::clamp(static_cast<int>(yVal + 1.772 * uVal), 0, 255);

                line[x] = qRgb(r, g, b);
            }
        }
    } else {
        img.fill(Qt::black);
    }

    return img;
}

// Child widget compositing all visible video layers bottom-to-top with each
// clip's transform (position/scale/rotation/opacity — docs/06 semantics,
// CPU preview path; the D3D12 render graph replaces the drawing internals
// later without changing this contract).
class VideoSurfaceWidget : public QWidget {
public:
    struct Layer {
        QImage image;
        velocity::engine::ClipTransform transform;
    };

    explicit VideoSurfaceWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setLayers(std::vector<Layer> layers) {
        layers_ = std::move(layers);
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(10, 10, 10)); // letterbox background
        if (layers_.empty()) {
            // Empty state: dark canvas with safe-area guides
            painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DashLine));
            painter.drawRect(rect().adjusted(20, 20, -20, -20));
            painter.drawLine(width() / 2 - 10, height() / 2, width() / 2 + 10, height() / 2);
            painter.drawLine(width() / 2, height() / 2 - 10, width() / 2, height() / 2 + 10);
            painter.setPen(QColor(100, 100, 100));
            painter.drawText(rect(), Qt::AlignCenter, "No Active Clip at Playhead");
            return;
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (const Layer& layer : layers_) {
            if (layer.image.isNull())
                continue;
            painter.save();
            const QSize fitSize = layer.image.size().scaled(size(), Qt::KeepAspectRatio);
            const QRectF baseRect(-fitSize.width() / 2.0, -fitSize.height() / 2.0,
                                  fitSize.width(), fitSize.height());
            // Normalized position offsets map to the fitted frame's dimensions.
            painter.translate(width() / 2.0 + layer.transform.posX * fitSize.width(),
                              height() / 2.0 + layer.transform.posY * fitSize.height());
            painter.rotate(layer.transform.rotation);
            painter.scale(layer.transform.scale, layer.transform.scale);
            painter.setOpacity(std::clamp(layer.transform.opacity, 0.0f, 1.0f));
            painter.drawImage(baseRect, layer.image);
            painter.restore();
        }
    }

private:
    std::vector<Layer> layers_;
};

// ------------------------------------------------------------- PreviewWidget

PreviewWidget::PreviewWidget(DocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session)
{
    // Tell Qt we are rendering natively via DirectX
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // Set size policy
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(320, 180);

    // Initialize Video Surface Widget
    videoSurface_ = new VideoSurfaceWidget(this);
    videoSurface_->setGeometry(rect());

    // Initialize DX12 Device
    auto devRes = gpu::Device::create(true);
    if (!devRes) {
        spdlog::error("PreviewWidget: Failed to initialize GPU Device: {}", devRes.error().message);
        return;
    }
    device_ = std::move(devRes.value());
    spdlog::info("PreviewWidget initialized device on: {}", QString::fromStdWString(device_->adapterName()).toStdString());

    connect(session_, &DocumentSession::playheadChanged, this, [this](Tick) {
        renderFrame();
    });
    connect(session_, &DocumentSession::snapshotChanged, this, [this](const engine::SnapshotPtr&) {
        renderFrame();
    });
}

PreviewWidget::~PreviewWidget() {
    if (device_) {
        device_->waitIdle();
    }
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (videoSurface_) {
        videoSurface_->setGeometry(rect());
    }
    if (!device_) return;

    // Release old swapchain to avoid resource leaks
    swapchain_.reset();

    // Create new swapchain using our winId (HWND)
    std::uint32_t w = static_cast<std::uint32_t>(width());
    std::uint32_t h = static_cast<std::uint32_t>(height());
    if (w == 0 || h == 0) return;

    auto scRes = gpu::Swapchain::create(*device_, reinterpret_cast<void*>(winId()), w, h);
    if (!scRes) {
        spdlog::error("PreviewWidget: Failed to recreate swapchain: {}", scRes.error().message);
        return;
    }
    swapchain_ = std::move(scRes.value());
    renderFrame();
}

void PreviewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    renderFrame();
}

void PreviewWidget::renderFrame() {
    // Composite every visible video layer at the playhead (bottom-to-top).
    auto seq = session_->currentSnapshot();
    const Tick playhead = session_->playhead();

    std::vector<VideoSurfaceWidget::Layer> layers;
    for (const auto& sample : engine::resolveVideoLayersAt(*seq, playhead)) {
        auto it = readers_.find(sample.asset);
        if (it == readers_.end()) {
            media::DecodeOptions opts;
            opts.preferHardware = true; // D3D11VA when available
            auto decRes = media::VideoDecoder::open(sample.asset, opts);
            it = readers_
                     .emplace(sample.asset,
                              decRes ? std::make_unique<media::SequentialFrameReader>(
                                           std::move(decRes.value()))
                                     : nullptr)
                     .first;
        }
        if (!it->second)
            continue;

        // Sequential-locality read: rolls forward during playback, seeks on
        // jumps (docs/04 §2).
        if (auto frameRes = it->second->at(sample.srcPts)) {
            QImage img = convertVideoFrameToQImage(*frameRes);
            if (!img.isNull())
                layers.push_back({std::move(img), sample.transform});
        }
    }

    if (videoSurface_)
        videoSurface_->setLayers(std::move(layers));

    // Keep the D3D12 swapchain presenting behind the composite.
    if (device_ && swapchain_) {
        const float clearColor[4] = {0.07f, 0.07f, 0.07f, 1.0f};
        auto res = swapchain_->clearAndPresent(*device_, clearColor);
        if (!res) {
            spdlog::error("PreviewWidget: Swapchain present failed: {}", res.error().message);
        }
    }
}

} // namespace velocity::ui
