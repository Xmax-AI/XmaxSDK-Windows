//
//  RemoteVideoBeautyPipeline.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Render/Beauty/VideoBeautyProcessor.hpp"
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>

namespace xmax::detail {
/** Shares one ordered output between preview and media while the optional effect warms independently. */
class RemoteVideoBeautyPipeline {
  public:
    using Output = std::function<void(std::shared_ptr<Frame>, uint64_t)>;
    explicit RemoteVideoBeautyPipeline(Output output, BeautyProcessorFactory factory = make_video_beauty_processor);
    ~RemoteVideoBeautyPipeline();
    void configure(std::optional<BeautyOptions> options);
    BeautyErrorListener listener(BeautyErrorListener value);
    void bind(uint64_t token, bool active);
    void submit(std::shared_ptr<Frame> frame, uint64_t token);
    // Full close resets options/listener; reconnection only invalidates frames via bind().
    void reset();
    void shutdown();
  private:
    enum class Phase { Off, Starting, FirstFrame, Warming, Active };
    struct Pending {
        std::shared_ptr<Frame> frame;
        uint64_t token, revision, sequence;
        bool sdr = false;
    };
    std::mutex mutex_;
    std::condition_variable inputReady_, effectReady_;
    std::deque<std::packaged_task<void()>> commands_;
    std::optional<Pending> input_, effectInput_;
    uint64_t token_ = 0, revision_ = 0, configuration_ = 0;
    uint64_t sequence_ = 0, lastOutput_ = 0;
    bool active_ = false, stopped_ = false;
    Phase phase_ = Phase::Off;
    // Only the effect thread creates, updates, uses and destroys the native processor.
    std::unique_ptr<VideoBeautyProcessor> processor_;
    BeautyErrorListener listener_;
    Output output_;
    BeautyProcessorFactory factory_;
    std::thread effectWorker_, inputWorker_;

    void prepareFrames();
    void runEffect();
    void apply(std::optional<BeautyOptions> options, uint64_t configuration);
    void process(Pending value);
    bool current(const Pending& value) const;
    void deliver(const Pending& value, std::shared_ptr<Frame> result);
};
}
