#include "velocity/gpu/window.h"

#include <windows.h>

namespace velocity::gpu {

namespace {
constexpr wchar_t kClassName[] = L"VelocityWindow";

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        // Mark closed via user data; destruction is owned by the Window object.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerClassOnce() {
    static bool done = false;
    if (done)
        return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    done = true;
}
} // namespace

Expected<std::unique_ptr<Window>, std::string>
Window::create(const wchar_t* title, std::uint32_t width, std::uint32_t height, bool visible) {
    using Ret = Expected<std::unique_ptr<Window>, std::string>;
    registerClassOnce();

    RECT r{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return Ret{makeUnexpected(std::string("CreateWindowExW failed: ") +
                                  std::to_string(GetLastError()))};

    if (visible)
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    auto w = std::unique_ptr<Window>(new Window());
    w->hwnd_ = hwnd;
    w->width_ = width;
    w->height_ = height;
    return Ret{std::move(w)};
}

Window::~Window() {
    if (hwnd_)
        DestroyWindow(static_cast<HWND>(hwnd_));
}

bool Window::pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA) != 0)
        closed_ = true;
    return !closed_;
}

} // namespace velocity::gpu
