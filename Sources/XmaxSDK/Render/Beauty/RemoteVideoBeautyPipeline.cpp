//
//  RemoteVideoBeautyPipeline.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Beauty/RemoteVideoBeautyPipeline.hpp"
#include "Render/Video/ResultVideoColor.hpp"

namespace xmax::detail {
RemoteVideoBeautyPipeline::RemoteVideoBeautyPipeline(Output output, BeautyProcessorFactory factory)
    : output_(std::move(output)), factory_(std::move(factory)), worker_([this] { run(); }) {}
RemoteVideoBeautyPipeline::~RemoteVideoBeautyPipeline() { shutdown(); }

void RemoteVideoBeautyPipeline::command(std::function<void()> action) {
    require(worker_.get_id() != std::this_thread::get_id(), XMAX_INVALID_STATE,
            "Do not block the beauty callback with a synchronous SDK operation");
    auto work = std::make_shared<std::packaged_task<void()>>(std::move(action));
    auto result = work->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        require(!stopped_, XMAX_INVALID_STATE, "Beauty pipeline has shut down");
        ++revision_;
        pending_.reset();
        commands_.push_back([work] { (*work)(); });
    }
    wake_.notify_one();
    result.get();
}

void RemoteVideoBeautyPipeline::configure(std::optional<BeautyOptions> options) {
    if (options) validate_beauty(*options);
    command([this, options] {
        if (!options) processor_.reset();
        else if (processor_) processor_->update(*options);
        else processor_ = factory_(*options);
        options_ = options;
    });
}

BeautyErrorListener RemoteVideoBeautyPipeline::listener(BeautyErrorListener value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(listener_, std::move(value));
}
void RemoteVideoBeautyPipeline::bind(uint64_t token, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_) return;
    token_ = token; active_ = active; ++revision_; pending_.reset();
}
void RemoteVideoBeautyPipeline::submit(std::shared_ptr<Frame> frame, uint64_t token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !active_ || token != token_) return;
        pending_ = Pending{std::move(frame), token, revision_};
    }
    wake_.notify_one();
}
void RemoteVideoBeautyPipeline::reset() {
    // Release foreign listener contexts outside the pipeline mutex.
    auto previous = listener({});
    command([this] {
        processor_.reset();
        options_.reset();
    });
}
void RemoteVideoBeautyPipeline::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true; active_ = false; ++revision_; pending_.reset();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    auto previous = listener({});
}
void RemoteVideoBeautyPipeline::run() {
    for (;;) {
        std::function<void()> control;
        std::optional<Pending> value;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopped_ || !commands_.empty() || pending_; });
            if (!commands_.empty()) { control = std::move(commands_.front()); commands_.pop_front(); }
            else if (stopped_) break;
            else value = std::exchange(pending_, {});
        }
        if (control) control();
        else if (value) process(std::move(*value));
    }
    processor_.reset();
}
void RemoteVideoBeautyPipeline::process(Pending value) {
    auto result = value.frame;
    const bool sdr = prepare_result_video_color(*result);
    std::optional<Error> failure;
    try {
        if (options_) {
            require(sdr, XMAX_MEDIA_ERROR, "Result beauty requires a supported SDR color space");
            if (!processor_) processor_ = factory_(*options_);
            result = processor_->process(*value.frame);
            // A temporary output-capacity limit drops this frame without disabling beauty.
            if (!result) return;
        }
    } catch (const Error& error) { failure = error; }
      catch (const std::exception& error) { failure = Error(XMAX_MEDIA_ERROR, error.what()); }
      catch (...) { failure = Error(XMAX_MEDIA_ERROR, "Result beauty failed"); }
    BeautyErrorListener notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !active_ || token_ != value.token || revision_ != value.revision) return;
        if (failure) { options_.reset(); notify = listener_; result = value.frame; }
    }
    if (failure) processor_.reset();
    // Never hold pipeline locks while delivering application callbacks.
    if (notify) { try { notify(*failure); } catch (...) {} }
    // Commit to the two internal consumers atomically with respect to settings/binding changes.
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || !active_ || token_ != value.token || revision_ != value.revision) return;
    output_(std::move(result), value.token);
}
}
