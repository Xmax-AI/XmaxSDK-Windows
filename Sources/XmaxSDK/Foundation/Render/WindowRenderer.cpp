#include "Foundation/Render/WindowRenderer.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
namespace xmax::detail {
bool native_window_available() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}
void render_window(const Frame& source, void* window, int32_t mode) {
    require(window && (mode == XMAX_CONTENT_FIT || mode == XMAX_CONTENT_FILL), XMAX_INVALID_ARGUMENT,
            "A native window and valid content mode are required");
#ifdef _WIN32
    const auto hwnd = static_cast<HWND>(window);
    require(IsWindow(hwnd), XMAX_INVALID_ARGUMENT, "Invalid HWND");
    require(GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId(), XMAX_INVALID_STATE,
            "Render on the HWND owner thread, normally from WM_PAINT");
    RECT bounds{};
    require(GetClientRect(hwnd, &bounds), XMAX_MEDIA_ERROR, "Cannot read the window bounds");
    const int width = bounds.right, height = bounds.bottom;
    if (width <= 0 || height <= 0)
        return;
    const auto frame = bgra_frame(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = LONG(frame.width);
    info.bmiHeader.biHeight = -LONG(frame.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const auto dc = GetDC(hwnd);
    require(dc != nullptr, XMAX_MEDIA_ERROR, "Cannot acquire the native display surface");
    const auto scale = mode == XMAX_CONTENT_FIT
                           ? std::min(double(width) / frame.width, double(height) / frame.height)
                           : std::max(double(width) / frame.width, double(height) / frame.height);
    const int target_width = int(std::round(frame.width * scale));
    const int target_height = int(std::round(frame.height * scale));
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, 0, width, height);
    FillRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetStretchBltMode(dc, HALFTONE);
    const auto result = StretchDIBits(dc, (width - target_width) / 2, (height - target_height) / 2,
                                      target_width, target_height, 0, 0, int(frame.width), int(frame.height),
                                      frame.planes[0].data(), &info, DIB_RGB_COLORS, SRCCOPY);
    if (saved)
        RestoreDC(dc, saved);
    ReleaseDC(hwnd, dc);
    require(result != int(GDI_ERROR), XMAX_MEDIA_ERROR, "Native frame presentation failed");
#else
    (void)source;
    throw Error(XMAX_UNSUPPORTED, "Native window presentation is provided by the Windows build");
#endif
}
} // namespace xmax::detail
