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
/** One in-flight frame and one latest pending frame, shared by preview and public media delivery. */
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
    struct Pending { std::shared_ptr<Frame> frame; uint64_t token, revision; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> commands_;
    std::optional<Pending> pending_;
    uint64_t token_ = 0, revision_ = 0;
    bool active_ = false, stopped_ = false;
    std::optional<BeautyOptions> options_;
    std::unique_ptr<VideoBeautyProcessor> processor_;
    BeautyErrorListener listener_;
    Output output_;
    BeautyProcessorFactory factory_;
    std::thread worker_;
    void run();
    void command(std::function<void()> action);
    void process(Pending value);
};
}
