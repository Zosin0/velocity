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

// Child widget that draws using QPainter
class VideoSurfaceWidget : public QWidget {
public:
    explicit VideoSurfaceWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }
    
    void setImage(const QImage& img) {
        image_ = img;
        update();
    }
    
protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        if (image_.isNull()) {
            // Draw default dark background
            painter.fillRect(rect(), QColor(18, 18, 18));
            
            // Draw visual guidelines / safe area
            painter.setPen(QPen(QColor(40, 40, 40), 1, Qt::DashLine));
            painter.drawRect(rect().adjusted(20, 20, -20, -20));
            
            // Draw center crosshairs
            painter.drawLine(width() / 2 - 10, height() / 2, width() / 2 + 10, height() / 2);
            painter.drawLine(width() / 2, height() / 2 - 10, width() / 2, height() / 2 + 10);
            
            painter.setPen(QColor(100, 100, 100));
            painter.drawText(rect(), Qt::AlignCenter, "No Active Clip at Playhead");
        } else {
            painter.fillRect(rect(), QColor(10, 10, 10)); // black letterboxes/pillarboxes
            QSize imgSize = image_.size();
            QSize fitSize = imgSize.scaled(size(), Qt::KeepAspectRatio);
            QRect targetRect((width() - fitSize.width()) / 2,
                             (height() - fitSize.height()) / 2,
                             fitSize.width(),
                             fitSize.height());
            painter.drawImage(targetRect, image_);
        }
    }
    
private:
    QImage image_;
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
    // 1. Resolve video sample at the playhead
    auto seq = session_->currentSnapshot();
    Tick playhead = session_->playhead();
    auto sampleOpt = engine::resolveVideoAt(*seq, playhead);

    if (sampleOpt) {
        const auto& sample = sampleOpt.value();
        // Check if we need to open a new decoder or switch assets
        if (currentAssetPath_ != sample.asset || !reader_) {
            media::DecodeOptions opts;
            opts.preferHardware = true; // Use D3D11VA hardware decode if available
            auto decRes = media::VideoDecoder::open(sample.asset, opts);
            if (decRes) {
                reader_ = std::make_unique<media::SequentialFrameReader>(std::move(decRes.value()));
                currentAssetPath_ = sample.asset;
            } else {
                reader_.reset();
                currentAssetPath_.clear();
            }
        }

        if (reader_) {
            // Sequential-locality read: rolls forward during playback,
            // seeks only on jumps (docs/04 §2).
            auto frameRes = reader_->at(sample.srcPts);
            if (frameRes) {
                QImage img = convertVideoFrameToQImage(frameRes.value());
                if (videoSurface_) {
                    videoSurface_->setImage(img);
                }
            } else {
                if (videoSurface_) {
                    videoSurface_->setImage(QImage()); // Fallback to black
                }
            }
        } else {
            if (videoSurface_) {
                videoSurface_->setImage(QImage());
            }
        }
    } else {
        // No clip at playhead
        if (videoSurface_) {
            videoSurface_->setImage(QImage());
        }
    }

    // Call Direct3D 12 swapchain clear and present to keep GPU pipeline active
    if (device_ && swapchain_) {
        const float clearColor[4] = {0.07f, 0.07f, 0.07f, 1.0f};
        auto res = swapchain_->clearAndPresent(*device_, clearColor);
        if (!res) {
            spdlog::error("PreviewWidget: Swapchain present failed: {}", res.error().message);
        }
    }
}

} // namespace velocity::ui
