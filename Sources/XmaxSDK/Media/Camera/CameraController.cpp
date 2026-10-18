#include "Media/Camera/CameraController.hpp"
#include "Foundation/Media/FrameConverter.hpp"
namespace xmax::detail {
void CameraController::start(const std::string& device, xmax_video_format format, CaptureCallbacks output,
                             const Cancel& cancel) {
    stop();
    capture_ = factory_();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        failure_.clear();
        last_timestamp_ = -1;
    }
    CaptureCallbacks callbacks;
    callbacks.frame = [this, format, output](Frame raw) {
        try {
            auto frame = fit_frame(std::move(raw), format);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                frame.timestamp_us = std::max(last_timestamp_ + 1, frame.timestamp_us);
                last_timestamp_ = frame.timestamp_us;
            }
            output.frame(std::move(frame));
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
                output.failure(Error(XMAX_MEDIA_ERROR, "Camera frame processing failed"));
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
        capture_->start(device, format, std::move(callbacks), cancel);
        const auto deadline = Clock::now() + std::chrono::seconds(12);
        std::unique_lock<std::mutex> lock(mutex_);
        while (!ready_) {
            require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
            require(failure_.empty(), XMAX_MEDIA_ERROR, failure_);
            require(Clock::now() < deadline, XMAX_TIMEOUT, "Camera produced no frames");
            changed_.wait_for(lock, std::chrono::milliseconds(20));
        }
        require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
    } catch (...) {
        stop();
        throw;
    }
}
void CameraController::stop() noexcept {
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }
}
} // namespace xmax::detail
