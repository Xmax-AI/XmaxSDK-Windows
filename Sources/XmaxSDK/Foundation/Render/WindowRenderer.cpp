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
#include <cstring>
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
 * Composites fading trails, pointer glow, rings, and particles above native-window video.
 *
 * @param[in] window An HWND owned by the calling thread.
 * @param[in] format The displayed video dimensions.
 * @param[in] content_mode The aspect-fit or aspect-fill policy shared with video.
 * @param[in] strokes Recent normalized strokes and fade ages.
 * @param[in] elapsed The current gesture age in seconds for animation.
 */
void render_trajectory_effects(
    void* window,
    xmax_video_format format,
    int32_t content_mode,
    const std::vector<TrajectoryStroke>& strokes,
    double elapsed
) {
#ifdef _WIN32
    if (strokes.empty())
        return;
    const auto hwnd = static_cast<HWND>(window);
    RECT bounds{};
    require(
        IsWindow(hwnd) && GetWindowThreadProcessId(hwnd, nullptr) == GetCurrentThreadId() &&
            GetClientRect(
                hwnd,
                &bounds
            ) &&
            format.width && format.height,
        XMAX_INVALID_ARGUMENT,
        "Trajectory effects require a valid owner-thread video window"
    );
    if (bounds.right <= 0 || bounds.bottom <= 0)
        return;
    const double sx = double(bounds.right) / format.width, sy = double(bounds.bottom) / format.height;
    const double scale = content_mode == XMAX_CONTENT_FIT ? std::min(sx, sy) : std::max(sx, sy);
    const double video_width = format.width * scale, video_height = format.height * scale;
    const double offset_x = (bounds.right - video_width) / 2, offset_y = (bounds.bottom - video_height) / 2;
    const auto dc = GetDC(hwnd);
    require(
        dc != nullptr,
        XMAX_MEDIA_ERROR,
        "Cannot acquire trajectory surface"
    );
    const double dpi = std::max(1.0, GetDeviceCaps(dc, LOGPIXELSX) / 96.0);
    int left = bounds.right, right = 0, top = bounds.bottom, bottom = 0;
    for (const auto& stroke : strokes)
        for (const auto& point : stroke.points) {
            const auto x = offset_x + point.x * video_width, y = offset_y + point.y * video_height;
            left = std::min(left, int(std::floor(x - 50 * dpi)));
            right = std::max(right, int(std::ceil(x + 50 * dpi)));
            top = std::min(top, int(std::floor(y - 50 * dpi)));
            bottom = std::max(bottom, int(std::ceil(y + 50 * dpi)));
        }
    left = std::max(left, int(std::ceil(std::max(0.0, offset_x))));
    top = std::max(top, int(std::ceil(std::max(0.0, offset_y))));
    right = std::min(right, int(std::floor(std::min(double(bounds.right), offset_x + video_width))));
    bottom = std::min(bottom, int(std::floor(std::min(double(bounds.bottom), offset_y + video_height))));
    const int width = right - left, height = bottom - top;
    if (width <= 0 || height <= 0) {
        ReleaseDC(
            hwnd,
            dc
        );
        return;
    }
    // Allocate before acquiring bitmap resources; render failures must still release the window DC.
    try {
        require(
            uint64_t(width) * height <= 16777216,
            XMAX_MEDIA_ERROR,
            "Trajectory surface exceeds the pixel budget"
        );
        TrajectoryBitmap effect(
            width,
            height
        );
        const auto map = [&](xmax_point point) {
            return xmax_point{offset_x + point.x * video_width - left, offset_y + point.y * video_height - top};
        };
        for (const auto& stroke : strokes) {
            const double opacity = std::pow(
                .95,
                std::max(0.0, stroke.age) * 60
            );
            if (stroke.points.size() == 1)
                effect.segment(
                    map(stroke.points.front()),
                    map(stroke.points.front()),
                    opacity,
                    dpi
                );
            for (size_t i = 1; i < stroke.points.size(); ++i)
                effect.segment(
                    map(stroke.points[i - 1]),
                    map(stroke.points[i]),
                    opacity,
                    dpi
                );
        }
        if (!strokes.back().points.empty()) {
            const double opacity = std::clamp(1 - strokes.back().age / .15, 0.0, 1.0);
            effect.pointer(
                map(strokes.back().points.back()),
                elapsed,
                opacity,
                dpi
            );
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        const auto bitmap = CreateDIBSection(
            dc,
            &info,
            DIB_RGB_COLORS,
            &pixels,
            nullptr,
            0
        );
        const auto memory = bitmap ? CreateCompatibleDC(dc) : nullptr;
        if (!bitmap || !memory) {
            if (bitmap)
                DeleteObject(bitmap);
            throw Error(
                XMAX_MEDIA_ERROR,
                "Cannot allocate trajectory compositing surface"
            );
        }
        std::memcpy(
            pixels,
            effect.pixels().data(),
            effect.pixels().size() * sizeof(uint32_t)
        );
        const auto old = SelectObject(
            memory,
            bitmap
        );
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const auto rendered = AlphaBlend(
            dc,
            left,
            top,
            width,
            height,
            memory,
            0,
            0,
            width,
            height,
            blend
        );
        SelectObject(
            memory,
            old
        );
        DeleteDC(memory);
        DeleteObject(bitmap);
        require(
            rendered != 0,
            XMAX_MEDIA_ERROR,
            "Cannot composite trajectory effects"
        );
    } catch (...) {
        ReleaseDC(
            hwnd,
            dc
        );
        throw;
    }
    ReleaseDC(
        hwnd,
        dc
    );
#else
    (void)window;
    (void)format;
    (void)content_mode;
    (void)strokes;
    (void)elapsed;
    throw Error(
        XMAX_UNSUPPORTED,
        "Native trajectory rendering is available on Windows only"
    );
#endif
}
} // namespace xmax::detail
