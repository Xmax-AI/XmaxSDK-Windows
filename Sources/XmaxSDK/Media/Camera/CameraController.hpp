//
//  CameraController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>

namespace xmax::detail {

/**
 * Starts native capture, preserves actual frame dimensions and format, and waits for the first real frame.
 */
class CameraController {

  public:

    /**
     * Retains the native camera adapter factory for enumeration and capture.
     */
    explicit CameraController(
        CameraFactory factory = make_camera_capture
    )
        : factory_(std::move(factory)) {
    }

    /**
     * Stops camera capture, drains callbacks, and releases owned media resources.
     */
    ~CameraController() {
        stop();
    }

    /**
     * Enumerates camera identifiers and names without starting capture.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     */
    std::vector<CameraDevice> devices() {
        return factory_()->devices();
    }

    /**
     * Starts native capture and delivers frames at their original dimensions, waiting for the first frame,
     * failure, or cancellation. Startup failures are thrown to the caller; only failures after successful
     * startup are delivered through the failure callback.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The requested pixel dimensions and frame rate, subject to the interface's format
     * limits.
     * @param[in] output The synchronous native-thread callbacks for capture frames and errors.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void
    start(
        const std::string& device,
        xmax_video_format format,
        CaptureCallbacks output,
        const Cancel& cancel
    );

    /**
     * Stops camera capture and drains frame and failure callbacks before returning.
     */
    void stop() noexcept;

  private:

    /**
     * Retains the first capture failure and forwards it only after startup has committed.
     *
     * @param[in] error The original capture or frame-processing failure.
     * @param[in] receiver The runtime failure receiver, invoked outside the camera-state lock.
     */
    void report_failure(
        Error error,
        const std::function<void(Error)>& receiver
    );

    /**
     * The native camera adapter factory for enumeration and capture.
     */
    CameraFactory factory_;

    /**
     * The exclusively owned native camera adapter.
     */
    std::unique_ptr<CameraCapture> capture_;

    /**
     * The mutex protecting readiness, failure reason, and the latest timestamp.
     */
    std::mutex mutex_;

    /**
     * Notifies waiters when the first frame arrives or capture fails.
     */
    std::condition_variable changed_;

    /**
     * Whether this capture binding has received and delivered its first valid frame.
     */
    bool ready_ = false;

    /**
     * Whether startup has committed and subsequent failures belong to the active camera stream.
     */
    bool running_ = false;

    /**
     * The first capture or conversion failure, including its original code and diagnostics.
     */
    std::optional<Error> failure_;

    /**
     * The latest accepted monotonic timestamp for this input binding, in microseconds.
     */
    int64_t last_timestamp_ = -1;
};
} // namespace xmax::detail
