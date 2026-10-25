//
//  CameraCapture.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CaptureCallbacks.hpp"
#include "Foundation/Media/Camera/CameraDevice.hpp"
#include "Foundation/Media/VideoFrame.hpp"

namespace xmax::detail {

/**
 * The native camera enumeration and capture interface; stop must drain callbacks before returning.
 */
class CameraCapture {

  public:

    /**
     * Releases the platform adapter after capture stops and callbacks drain.
     *
     * @note Finish concurrent calls using this object before destroying it.
     */
    virtual ~CameraCapture() = default;

    /**
     * Enumerates device identifiers, display names, and the default camera without starting capture.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     */
    virtual std::vector<CameraDevice> devices() = 0;

    /**
     * Starts cancellable native capture and delivers independently owned frames through callbacks.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The desired encoding dimensions and capture frame rate; frame callbacks report
     * actual dimensions.
     * @param[in] callbacks Capture frame and failure callbacks, delivered on the native media thread and
     * drained before stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    virtual void start(
        const std::string& device,
        xmax_video_format format,
        CaptureCallbacks callbacks,
        const Cancel& cancel
    ) = 0;

    /**
     * Stops native capture and drains all frame and failure callbacks before returning.
     */
    virtual void stop() noexcept = 0;
};

/**
 * The factory for exclusively owned native camera adapters; must not return an empty object.
 */
using CameraFactory = std::function<std::unique_ptr<CameraCapture>()>;

/**
 * Creates a native camera adapter or reports unsupported capability on other platforms.
 *
 * @return An exclusively owned camera adapter; unsupported platforms throw a capability error.
 */
std::unique_ptr<CameraCapture> make_camera_capture();

/**
 * Reports whether this SDK build provides native camera capture.
 */
bool camera_available() noexcept;
} // namespace xmax::detail
