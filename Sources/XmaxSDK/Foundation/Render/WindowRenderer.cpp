//
//  WindowRenderer.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Render/WindowRenderer.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif
namespace xmax::detail {

/**
 * Reports whether this build supports native HWND rendering.
 */
bool native_window_available() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

/**
 * Renders an owned video frame with aspect fit or fill on the HWND owner thread.
 *
 * @param[in] source The frame or borrowed view to read, with a valid pixel layout.
 * @param[in] window The target Windows HWND as void*, owned by the calling thread.
 * @param[in] mode XMAX_CONTENT_FIT for aspect fit, or XMAX_CONTENT_FILL for centered aspect fill.
 */
void render_window(
    const Frame& source,
    void* window,
    int32_t mode
) {
    require(
        window && (mode == XMAX_CONTENT_FIT || mode == XMAX_CONTENT_FILL),
        XMAX_INVALID_ARGUMENT,
        "A native window and valid content mode are required"
    );
#ifdef _WIN32

    const auto hwnd = static_cast<HWND>(window);
    require(
        IsWindow(hwnd),
        XMAX_INVALID_ARGUMENT,
        "Invalid HWND"
    );
    require(
        GetWindowThreadProcessId(
            hwnd,
            nullptr
        ) == GetCurrentThreadId(),
        XMAX_INVALID_STATE,
        "Render on the HWND owner thread, normally from WM_PAINT"
    );

    RECT bounds{};
    require(
        GetClientRect(
            hwnd,
            &bounds
        ),
        XMAX_MEDIA_ERROR,
        "Cannot read the window bounds"
    );

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
    require(
        dc != nullptr,
        XMAX_MEDIA_ERROR,
        "Cannot acquire the native display surface"
    );

    const auto scale = mode == XMAX_CONTENT_FIT
                           ? std::min(double(width) / frame.width, double(height) / frame.height)
                           : std::max(double(width) / frame.width, double(height) / frame.height);
    const int target_width = int(std::round(frame.width * scale));
    const int target_height = int(std::round(frame.height * scale));
    const int saved = SaveDC(dc);
    IntersectClipRect(
        dc,
        0,
        0,
        width,
        height
    );
    FillRect(
        dc,
        &bounds,
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))
    );
    SetStretchBltMode(
        dc,
        HALFTONE
    );

    const auto result = StretchDIBits(
        dc,
        (width - target_width) / 2 + (source.presentation_mirrored ? target_width : 0),
        (height - target_height) / 2,
        source.presentation_mirrored ? -target_width : target_width,
        target_height,
        0,
        0,
        int(frame.width),
        int(frame.height),
        frame.planes[0].data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY
    );

    if (saved)
        RestoreDC(
            dc,
            saved
        );

    ReleaseDC(
        hwnd,
        dc
    );
    require(
        result != int(GDI_ERROR),
        XMAX_MEDIA_ERROR,
        "Native frame presentation failed"
    );
#else
    (void)source;
    throw Error(
        XMAX_UNSUPPORTED,
        "Native window presentation is provided by the Windows build"
    );
#endif
}

/**
 * Draws default trajectory lines and points over an HWND's video content.
 *
 * @param[in] window A Windows HWND owned by the calling thread.
 * @param[in] format The actual dimensions of the displayed video.
 * @param[in] content_mode The aspect-fit or aspect-fill layout mode.
 * @param[in] points The normalized trajectory in [0, 1].
 */
void render_trajectory(
    void* window,
    xmax_video_format format,
    int32_t content_mode,
    const std::vector<xmax_point>& points
) {
#ifdef _WIN32
    if (points.empty())
        return;

    const auto hwnd = static_cast<HWND>(window);
    require(
        IsWindow(hwnd) && GetWindowThreadProcessId(
            hwnd,
            nullptr
        ) == GetCurrentThreadId(),
        XMAX_INVALID_ARGUMENT,
        "Trajectory rendering requires an HWND owned by the calling thread"
    );

    RECT bounds{};
    require(
        GetClientRect(
            hwnd,
            &bounds
        ) && format.width &&
            format.height,
        XMAX_INVALID_ARGUMENT,
        "Invalid trajectory surface"
    );

    const auto sx = double(bounds.right) / format.width;
    const auto sy = double(bounds.bottom) / format.height;
    const auto scale = content_mode == XMAX_CONTENT_FIT ? std::min(sx, sy) : std::max(sx, sy);
    const auto width = format.width * scale, height = format.height * scale;
    const auto left = (bounds.right - width) / 2, top = (bounds.bottom - height) / 2;

    const auto dc = GetDC(hwnd);
    require(
        dc != nullptr,
        XMAX_MEDIA_ERROR,
        "Cannot acquire trajectory surface"
    );
    const auto saved = SaveDC(dc);
    IntersectClipRect(
        dc,
        0,
        0,
        bounds.right,
        bounds.bottom
    );
    const auto pen = CreatePen(
        PS_SOLID,
        3,
        RGB(84, 99, 255)
    );
    const auto brush = CreateSolidBrush(RGB(84, 99, 255));
    const auto old_pen = SelectObject(
        dc,
        pen
    );
    const auto old_brush = SelectObject(
        dc,
        brush
    );

    bool first = true;
    for (const auto& point : points) {
        const int x = int(std::round(left + point.x * width));
        const int y = int(std::round(top + point.y * height));
        if (first)
            MoveToEx(
                dc,
                x,
                y,
                nullptr
            );
        else
            LineTo(
                dc,
                x,
                y
            );
        Ellipse(
            dc,
            x - 4,
            y - 4,
            x + 4,
            y + 4
        );
        first = false;
    }

    SelectObject(
        dc,
        old_pen
    );
    SelectObject(
        dc,
        old_brush
    );
    DeleteObject(pen);
    DeleteObject(brush);
    if (saved)
        RestoreDC(
            dc,
            saved
        );
    ReleaseDC(
        hwnd,
        dc
    );
#else
    (void)window;
    (void)format;
    (void)content_mode;
    (void)points;
    throw Error(
        XMAX_UNSUPPORTED,
        "Native trajectory presentation is provided by the Windows build"
    );
#endif
}
} // namespace xmax::detail
