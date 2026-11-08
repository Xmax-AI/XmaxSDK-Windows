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
#include "Media/Video/VideoFrameScaler.hpp"
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

CameraController::~CameraController() {
    stop();
    {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        capture_exiting_ = true;
    }
    capture_changed_.notify_one();
    if (capture_worker_.joinable())
        capture_worker_.join();
}

std::future<void> CameraController::queue_capture(
    std::packaged_task<void()> work,
    bool start_worker
) {
    auto result = work.get_future();
    {
        std::lock_guard<std::mutex> lock(capture_mutex_);
        if (!capture_worker_.joinable()) {
            if (!start_worker)
                return {};
            capture_worker_ = std::thread([this] { capture_loop(); });
        }
        capture_work_.push_back(std::move(work));
    }
    capture_changed_.notify_one();
    return result;
}

void CameraController::capture_loop() {
    for (;;) {
        std::packaged_task<void()> work;
        {
            std::unique_lock<std::mutex> lock(capture_mutex_);
            capture_changed_.wait(lock, [this] { return capture_exiting_ || !capture_work_.empty(); });
            if (capture_work_.empty())
                return;
            work = std::move(capture_work_.front());
            capture_work_.pop_front();
        }
        work();
    }
}

/**
 * Starts native capture and delivers frames at the requested dimensions, waiting for the first frame,
 * failure, or cancellation. Startup failures are thrown to the caller; only failures after successful
 * startup are delivered through the failure callback.
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        running_ = false;
        failure_.reset();
        last_timestamp_ = -1;
    }

    const auto metrics = std::make_shared<CameraMetrics>();
    CaptureCallbacks callbacks;
    callbacks.frame = [this, output, metrics, format](Frame frame) {
        try {
            const bool profiling = XmaxLogger::is_enabled(XMAX_LOG_PERFORMANCE);
            const auto begin = profiling ? Clock::now() : Clock::time_point{};
            const auto width = frame.width, height = frame.height;
            if (frame.width == format.width && frame.height == format.height) {
                validate_frame(frame.view());
            } else {
                auto scaled = copy_scaled_video_frame(frame.view(), format, false);
                scaled.presentation_mirrored = frame.presentation_mirrored;
                scaled.color_primaries = frame.color_primaries;
                scaled.color_transfer = frame.color_transfer;
                frame = std::move(scaled);
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (failure_)
                    return;
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
            report_failure(
                Error(
                    XMAX_MEDIA_ERROR,
                    "Camera frame processing failed"
                ),
                output.failure
            );
        }
    };
    callbacks.failure = [this, output](Error error) {
        report_failure(
            std::move(error),
            output.failure
        );
    };

    auto binding = std::make_shared<Recovery>();
    binding->device = device;
    binding->format = format;
    binding->callbacks = std::move(callbacks);
    binding->callbacks.interrupted = [this, weak = std::weak_ptr<Recovery>(binding)] {
        if (auto current = weak.lock())
            interrupted(current);
    };
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recovery_ = binding;
    }

    try {
        queue_capture(std::packaged_task<void()>([&, binding] {
            require(!cancel() && !binding->cancelled, XMAX_CANCELLED, "Camera startup cancelled");
            capture_ = factory_();
            capture_->start(device, format, binding->callbacks, cancel);
        }), true).get();

        const auto deadline = Clock::now() + std::chrono::seconds(12);
        std::unique_lock<std::mutex> lock(mutex_);

        for (;;) {
            require(
                !cancel(),
                XMAX_CANCELLED,
                "Camera startup cancelled"
            );
            if (failure_)
                throw *failure_;
            if (ready_)
                break;

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

        // Commit under the failure lock so each error belongs to startup or runtime, never both.
        running_ = true;
        lock.unlock();

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

void CameraController::interrupted(const std::shared_ptr<Recovery>& binding) {
    bool startup = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (binding->cancelled || recovery_ != binding || failure_)
            return;
        binding->interrupted = true;
        if (binding->retrying) {
            changed_.notify_all();
            return;
        }
        startup = !running_;
        if (!startup)
            binding->retrying = true;
    }
    if (startup) {
        binding->callbacks.failure(Error(XMAX_MEDIA_ERROR, "Camera capture was interrupted during startup"));
        return;
    }
    queue_capture(std::packaged_task<void()>([this, binding] { recover_capture(binding); }), false);
}

void CameraController::recover_capture(const std::shared_ptr<Recovery>& binding) {
    const Cancel cancelled = [binding] { return binding->cancelled.load(); };
    for (int attempt = 0; attempt < 3 && !cancelled(); ++attempt) {
        if (capture_) {
            capture_->stop();
            capture_.reset();
        }
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (changed_.wait_for(lock, std::chrono::milliseconds(250), [&] { return cancelled() || failure_.has_value(); }))
                return;
            ready_ = false;
            binding->interrupted = false;
        }
        try {
            capture_ = factory_();
            capture_->start(binding->device, binding->format, binding->callbacks, cancelled);
        } catch (const std::exception&) {
            continue;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::seconds(1), [&] {
            return ready_ || cancelled() || binding->interrupted || failure_.has_value();
        });
        if (cancelled() || failure_)
            return;
        if (ready_ && !binding->interrupted) {
            binding->retrying = false;
            return;
        }
    }
    if (!cancelled())
        binding->callbacks.failure(Error(XMAX_MEDIA_ERROR, "Camera capture did not resume after interruption"));
}

/**
 * Retains the first capture failure and forwards it only after startup has committed.
 *
 * @param[in] error The original capture or frame-processing failure.
 * @param[in] receiver The runtime failure receiver, invoked outside the camera-state lock.
 */
void CameraController::report_failure(
    Error error,
    const std::function<void(Error)>& receiver
) {
    bool notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_)
            return;

        failure_ = error;
        notify = running_;
    }
    changed_.notify_all();

    if (notify && receiver)
        receiver(std::move(error));
}

/**
 * Stops camera capture and drains frame and failure callbacks before returning.
 */
void CameraController::stop() noexcept {
    auto stopped = stop_async();
    if (stopped.valid())
        stopped.wait();
}

std::future<void> CameraController::stop_async() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recovery_)
            recovery_->cancelled = true;
    }
    changed_.notify_all();
    return queue_capture(std::packaged_task<void()>([this] { stop_capture(); }), false);
}

void CameraController::stop_capture() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        recovery_.reset();
    }

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
