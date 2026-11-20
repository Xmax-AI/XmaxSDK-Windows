//
//  RemoteVideoInterpolationPipeline.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Render/Interpolation/VideoInterpolationProcessor.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>

namespace xmax::detail {

/** Bounded inference and timestamp-based preview delivery, independent of the RTC producer. */
class RemoteVideoInterpolationPipeline {
  public:
    using Output = std::function<void(std::shared_ptr<Frame>, uint64_t)>;
    explicit RemoteVideoInterpolationPipeline(Output output,
        InterpolationProcessorFactory factory = make_video_interpolation_processor);
    ~RemoteVideoInterpolationPipeline();
    void configure(std::optional<InterpolationOptions> options);
    xmax_video_interpolation_status status() const;
    void bind(uint64_t token, bool active);
    void submit(std::shared_ptr<Frame> frame, uint64_t token);
    void shutdown();

  private:
    struct Pair {
        std::shared_ptr<Frame> previous, next;
        uint64_t token, revision;
        Clock::time_point deadline;
    };
    struct Presentation {
        std::shared_ptr<Frame> frame;
        Clock::time_point due;
        bool intermediate;
    };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> commands_;
    std::optional<Pair> pending_;
    std::deque<Presentation> presentations_;
    size_t queued_bytes_ = 0;
    std::shared_ptr<Frame> previous_;
    Clock::time_point anchor_{};
    int64_t first_timestamp_ = 0;
    uint64_t token_ = 0, revision_ = 0;
    bool active_ = false, stopped_ = false;
    uint32_t buffer_ms_ = 125;
    xmax_video_interpolation_status status_{};
    Output output_;
    InterpolationProcessorFactory factory_;
    std::unique_ptr<VideoInterpolationProcessor> processor_;
    uint64_t processor_revision_ = 0; // Accessed only by the inference thread.
    std::thread inference_, presenter_;
    void clear_locked();
    bool current_locked(const Pair& pair) const;
    bool schedule_locked(std::shared_ptr<Frame> frame, Clock::time_point due, bool intermediate);
    void infer();
    void present();
    void process(Pair pair);
};

} // namespace xmax::detail
