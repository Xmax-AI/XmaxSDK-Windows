//
//  CameraController.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Media/Camera/CameraController.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"

namespace xmax::detail {
namespace {

/**
 * Aggregates camera validation and synchronous delivery costs by time window rather than logging each frame.
 */
struct CameraMetrics {

    /**
     * The mutex protecting the sampling window and accumulated frame metrics.
     */
    std::mutex mutex;

    /**
     * The configuration revision used to discard records from earlier configurations.
     */
    uint64_t generation = 0;

    /**
     * The recorded frame count in the current sampling window.
     */
    uint64_t count = 0;

    /**
     * The sampling window's monotonic start time.
     */
    Clock::time_point since{};

    /**
     * The window's accumulated frame-validation time, in milliseconds.
     */
    double validation_ms = 0;

    /**
     * The window's accumulated synchronous delivery time, in milliseconds.
     */
    double delivery_ms = 0;

    /**
     * The maximum per-frame validation time in the window, in milliseconds.
     */
    double validation_max_ms = 0;

    /**
     * The maximum per-frame synchronous delivery time in the window, in milliseconds.
     */
    double delivery_max_ms = 0;

    /**
     * Aggregates actual frame rate, validation time, and synchronous delivery time over two-second windows.
     *
     * @param[in] begin The monotonic time when frame validation started.
     * @param[in] validated The monotonic time when frame validation finished.
     * @param[in] delivered The monotonic time when synchronous delivery finished.
     * @param[in] width The input width, in pixels.
     * @param[in] height The input height, in pixels.
     */
    void record(
        Clock::time_point begin,
        Clock::time_point validated,
        Clock::time_point delivered,
        uint32_t width,
        uint32_t height
    ) noexcept {
        try {
            const auto config = XmaxLogger::configuration();
            std::lock_guard<std::mutex> lock(mutex);

            if (since == Clock::time_point{} || generation != config.generation) {
                generation = config.generation;
                since = begin;
                count = 0;
                validation_ms = delivery_ms = validation_max_ms = delivery_max_ms = 0;
            }

            ++count;

            const auto validation = std::chrono::duration<double, std::milli>(validated - begin).count();
            const auto delivery = std::chrono::duration<double, std::milli>(delivered - validated).count();
            validation_ms += validation;
            delivery_ms += delivery;
            validation_max_ms = std::max(validation_max_ms, validation);
            delivery_max_ms = std::max(delivery_max_ms, delivery);

            const auto seconds = std::chrono::duration<double>(delivered - since).count();

            if (seconds < 2)
                return;

            XmaxLogger::write(
                LogCategory::Media,
                XMAX_LOG_DEBUG,
                [&](auto language) {
                    return XmaxLogger::localized(
                               language,
                               "摄像头处理性能 (Camera Pipeline Performance)",
                               "Camera Pipeline Performance"
                           ) +
                           "\n├─ input=" + std::to_string(width) + "x" + std::to_string(height) +
                           ", output_fps=" + std::to_string(count / seconds) +
                           "\n├─ validation_avg_ms=" + std::to_string(validation_ms / count) +
                           ", validation_max_ms=" + std::to_string(validation_max_ms) +
                           "\n└─ delivery_avg_ms=" + std::to_string(delivery_ms / count) +
                           ", delivery_max_ms=" + std::to_string(delivery_max_ms);
                },
                XMAX_LOG_PERFORMANCE
            );
            since = {};
        } catch (...) {
            // Keep diagnostic failures from interrupting capture and publication.
        }
    }
};
} // namespace

/**
 * Starts native capture and delivers frames at their original dimensions, waiting for the first frame,
 * failure, or cancellation.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 * @param[in] format The requested pixel dimensions and frame rate, subject to the interface's format
 * limits.
 * @param[in] output The synchronous native-thread callbacks for capture frames and errors.
 * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
 */
void CameraController::start(
    const std::string& device,
    xmax_video_format format,
    CaptureCallbacks output,
    const Cancel& cancel
) {
    stop();
    XmaxLogger::write(
        LogCategory::Media,
        XMAX_LOG_INFO,
        [&](auto language) {
            return XmaxLogger::localized(
                       language,
                       "准备摄像头 (Prepare Camera)",
                       "Prepare Camera"
                   ) +
                   "\n└─ " +
                   std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
                   std::to_string(format.fps) + " fps";
        }
    );
    capture_ = factory_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        failure_.clear();
        last_timestamp_ = -1;
    }

    const auto metrics = std::make_shared<CameraMetrics>();
    CaptureCallbacks callbacks;
    callbacks.frame = [this, output, metrics](Frame frame) {
        try {
            const bool profiling = XmaxLogger::is_enabled(XMAX_LOG_PERFORMANCE);
            const auto begin = profiling ? Clock::now() : Clock::time_point{};
            const auto width = frame.width, height = frame.height;
            validate_frame(frame.view());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                frame.timestamp_us = std::max(last_timestamp_ + 1, frame.timestamp_us);
                last_timestamp_ = frame.timestamp_us;
            }

            const auto validated = profiling ? Clock::now() : Clock::time_point{};
            output.frame(std::move(frame));

            if (profiling)
                metrics->record(
                    begin,
                    validated,
                    Clock::now(),
                    width,
                    height
                );

            {
                std::lock_guard<std::mutex> lock(mutex_);
                ready_ = true;
            }
            changed_.notify_all();
        } catch (const std::exception&) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                failure_ = "Camera frame processing failed";
            }
            changed_.notify_all();
            if (output.failure)
                output.failure(Error(
                    XMAX_MEDIA_ERROR,
                    "Camera frame processing failed"
                ));
        }
    };
    callbacks.failure = [this, output](Error error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failure_ = error.what();
        }
        changed_.notify_all();
        if (output.failure)
            output.failure(std::move(error));
    };

    try {
        capture_->start(
            device,
            format,
            std::move(callbacks),
            cancel
        );

        const auto deadline = Clock::now() + std::chrono::seconds(12);
        std::unique_lock<std::mutex> lock(mutex_);

        while (!ready_) {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "Camera startup cancelled"
            );
            require(
                failure_.empty(),
                XMAX_MEDIA_ERROR,
                failure_
            );
            require(
                Clock::now() < deadline,
                XMAX_TIMEOUT,
                "Camera produced no frames"
            );
            changed_.wait_for(
                lock,
                std::chrono::milliseconds(20)
            );
        }

        require(
            !cancel(),
            XMAX_CANCELLED,
            "Camera startup cancelled"
        );
        XmaxLogger::write(
            LogCategory::Media,
            XMAX_LOG_INFO,
            [](auto language) {
                return XmaxLogger::localized(
                    language,
                    "摄像头首帧就绪 (Camera First Frame Ready)",
                    "Camera First Frame Ready"
                );
            }
        );
    } catch (...) {
        stop();
        throw;
    }
}

/**
 * Stops camera capture and drains frame and failure callbacks before returning.
 */
void CameraController::stop() noexcept {
    if (capture_) {
        capture_->stop();
        capture_.reset();
        XmaxLogger::write(
            LogCategory::Media,
            XMAX_LOG_INFO,
            [](auto language) {
                return XmaxLogger::localized(
                    language,
                    "摄像头已停止 (Camera Stopped)",
                    "Camera Stopped"
                );
            }
        );
    }
}
} // namespace xmax::detail
