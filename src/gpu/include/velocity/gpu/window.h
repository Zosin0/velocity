#pragma once
// Spike-B Win32 window host for the preview swapchain. NOTE: the product UI
// framework decision (Qt 6 per docs/01 vs. descope, pending in PROGRESS.md)
// does not change this class's role — under Qt the swapchain still targets a
// raw HWND we own; only the surrounding chrome differs.

#include <velocity/foundation/expected.h>

#include <cstdint>
#include <memory>
#include <string>

namespace velocity::gpu {

class Window {
public:
    static Expected<std::unique_ptr<Window>, std::string>
    create(const wchar_t* title, std::uint32_t width, std::uint32_t height, bool visible);
    ~Window();

    [[nodiscard]] void* hwnd() const { return hwnd_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }

    // Drains pending messages; returns false once WM_CLOSE/WM_DESTROY arrived.
    bool pumpMessages();

private:
    Window() = default;
    void* hwnd_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool closed_ = false;
};

} // namespace velocity::gpu
