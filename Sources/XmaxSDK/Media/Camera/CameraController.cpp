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
 * 按时间窗口汇总摄像头帧校验及同步交付成本，避免逐帧输出日志。
 */
struct CameraMetrics {

    /**
     * 保护性能采样窗口和累计帧处理指标的互斥锁。
     */
    std::mutex mutex;

    /**
     * 配置版本，用于丢弃旧配置下产生的日志。
     */
    uint64_t generation = 0;

    /**
     * 当前性能采样窗口内已记录的视频帧数量。
     */
    uint64_t count = 0;

    /**
     * 当前性能采样窗口的单调时钟起点。
     */
    Clock::time_point since{};

    /**
     * 当前窗口帧校验累计耗时，单位为毫秒。
     */
    double validation_ms = 0;

    /**
     * 当前窗口同步媒体交付累计耗时，单位为毫秒。
     */
    double delivery_ms = 0;

    /**
     * 当前窗口单帧校验最大耗时，单位为毫秒。
     */
    double validation_max_ms = 0;

    /**
     * 当前窗口单帧同步媒体交付最大耗时，单位为毫秒。
     */
    double delivery_max_ms = 0;

    /**
     * 按两秒窗口汇总实际帧率、校验耗时和同步交付耗时。
     *
     * @param[in] begin 当前帧开始校验的单调时间点。
     * @param[in] validated 当前帧完成校验的单调时间点。
     * @param[in] delivered 当前帧完成同步交付的单调时间点。
     * @param[in] width 输入视频宽度，单位为像素。
     * @param[in] height 输入视频高度，单位为像素。
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
                LogCategory::Media, XMAX_LOG_DEBUG,
                [&](auto language) {
                    return XmaxLogger::localized(
                               language, "摄像头处理性能 (Camera Pipeline Performance)",
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
            // 隔离诊断异常，确保视频采集和发布继续执行。
        }
    }
};
} // namespace

/**
 * 启动原生采集并直接交付原始尺寸的视频帧，等待首帧、错误或取消。
 *
 * @param[in] device UTF-8 摄像头设备标识，空文本选择默认摄像头。
 * @param[in] format 请求的视频像素宽高及每秒帧数，须符合调用接口的规格限制。
 * @param[in] output 接收原始采集帧和采集错误的回调，在原生媒体线程同步触发。
 * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
 */
void CameraController::start(
    const std::string& device,
    xmax_video_format format,
    CaptureCallbacks output,
    const Cancel& cancel
) {
    stop();
    XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [&](auto language) {
        return XmaxLogger::localized(language, "准备摄像头 (Prepare Camera)", "Prepare Camera") + "\n└─ " +
               std::to_string(format.width) + " × " + std::to_string(format.height) + " @ " +
               std::to_string(format.fps) + " fps";
    });
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
                metrics->record(begin, validated, Clock::now(), width, height);

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
        XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [](auto language) {
            return XmaxLogger::localized(
                language, "摄像头首帧就绪 (Camera First Frame Ready)", "Camera First Frame Ready"
            );
        });
    } catch (...) {
        stop();
        throw;
    }
}

/**
 * 停止摄像头采集，并在返回前排空帧与错误回调。
 */
void CameraController::stop() noexcept {
    if (capture_) {
        capture_->stop();
        capture_.reset();
        XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [](auto language) {
            return XmaxLogger::localized(language, "摄像头已停止 (Camera Stopped)", "Camera Stopped");
        });
    }
}
} // namespace xmax::detail
