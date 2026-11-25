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
    : output_(std::move(output)), factory_(std::move(factory)), inputWorker_([this] { prepareFrames(); }) {}
RemoteVideoBeautyPipeline::~RemoteVideoBeautyPipeline() { shutdown(); }

void RemoteVideoBeautyPipeline::configure(std::optional<BeautyOptions> options) {
    if (options) validate_beauty(*options);
    std::future<void> completion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        require(!stopped_, XMAX_INVALID_STATE, "Beauty pipeline has shut down");
        require(effectWorker_.get_id() != std::this_thread::get_id(), XMAX_INVALID_STATE,
                "Do not block the beauty callback with a synchronous SDK operation");
        if (!effectWorker_.joinable()) {
            if (!options) {
                ++revision_;
                input_.reset();
                return;
            }
            effectWorker_ = std::thread([this] { runEffect(); });
        }
        auto& command = commands_.emplace_back([this, options, configuration = ++configuration_] {
            apply(options, configuration);
        });
        completion = command.get_future();
        ++revision_;
        input_.reset();
        effectInput_.reset();
        if (!options) phase_ = Phase::Off;
        else if (phase_ != Phase::Active) phase_ = Phase::Starting;
    }
    effectReady_.notify_one();
    // Preserve synchronous configuration errors without holding up video delivery.
    completion.get();
}

void RemoteVideoBeautyPipeline::apply(std::optional<BeautyOptions> options, uint64_t configuration) {
    try {
        if (!options) processor_.reset();
        else if (processor_) processor_->update(*options);
        else processor_ = factory_(*options);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (configuration == configuration_ && phase_ == Phase::Starting) {
            phase_ = processor_ ? Phase::FirstFrame : Phase::Off;
            effectInput_.reset();
        }
        throw;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (configuration == configuration_ && phase_ == Phase::Starting)
        phase_ = effectInput_ ? Phase::Warming : Phase::FirstFrame;
}

BeautyErrorListener RemoteVideoBeautyPipeline::listener(BeautyErrorListener value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(listener_, std::move(value));
}
void RemoteVideoBeautyPipeline::bind(uint64_t token, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_) return;
    token_ = token;
    active_ = active;
    ++revision_;
    input_.reset();
    effectInput_.reset();
    if (phase_ == Phase::Warming) phase_ = Phase::FirstFrame;
}
void RemoteVideoBeautyPipeline::submit(std::shared_ptr<Frame> frame, uint64_t token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || !active_ || token != token_) return;
        input_ = Pending{std::move(frame), token, revision_, ++sequence_};
    }
    inputReady_.notify_one();
}
void RemoteVideoBeautyPipeline::reset() {
    auto previous = listener({});
    configure(std::nullopt);
}
void RemoteVideoBeautyPipeline::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        active_ = false;
        input_.reset();
        effectInput_.reset();
    }
    inputReady_.notify_one();
    effectReady_.notify_one();
    if (inputWorker_.joinable()) inputWorker_.join();
    if (effectWorker_.joinable()) effectWorker_.join();
    auto previous = listener({});
}

// The caller holds mutex_ when checking binding, settings and output order.
bool RemoteVideoBeautyPipeline::current(const Pending& value) const {
    return !stopped_ && active_ && value.token == token_ && value.revision == revision_;
}
void RemoteVideoBeautyPipeline::deliver(const Pending& value, std::shared_ptr<Frame> result) {
    if (value.sequence <= lastOutput_) return;
    lastOutput_ = value.sequence;
    output_(std::move(result), value.token);
}
void RemoteVideoBeautyPipeline::prepareFrames() {
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        inputReady_.wait(lock, [this] { return stopped_ || input_.has_value(); });
        if (stopped_) return;
        auto value = std::move(*input_);
        input_.reset();
        lock.unlock();
        value.sdr = prepare_result_video_color(*value.frame);
        lock.lock();
        if (!current(value)) continue;

        // During construction keep the freshest prepared input for priming. If a configured
        // but idle stream supplies only one frame, submit it directly as its first effect frame.
        const auto phase = phase_;
        if (phase == Phase::Starting || phase == Phase::FirstFrame || phase == Phase::Active) {
            effectInput_ = value;
            if (phase == Phase::FirstFrame) phase_ = Phase::Warming;
            effectReady_.notify_one();
        }
        if (phase == Phase::Off || phase == Phase::Starting || phase == Phase::Warming)
            deliver(value, value.frame);
    }
}

void RemoteVideoBeautyPipeline::runEffect() {
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        effectReady_.wait(lock, [this] { return stopped_ || !commands_.empty() || effectInput_.has_value(); });
        if (!commands_.empty()) {
            auto command = std::move(commands_.front());
            commands_.pop_front();
            lock.unlock();
            command();
        } else if (stopped_) {
            break;
        } else {
            auto value = std::move(*effectInput_);
            effectInput_.reset();
            lock.unlock();
            process(std::move(value));
        }
    }
    processor_.reset();
}
void RemoteVideoBeautyPipeline::process(Pending value) {
    std::optional<Error> failure;
    std::shared_ptr<Frame> result;
    try {
        require(value.sdr, XMAX_MEDIA_ERROR, "Result beauty requires a supported SDR color space");
        result = processor_->process(*value.frame);
    } catch (const Error& error) { failure = error; }
      catch (const std::exception& error) { failure = Error(XMAX_MEDIA_ERROR, error.what()); }
      catch (...) { failure = Error(XMAX_MEDIA_ERROR, "Result beauty failed"); }

    BeautyErrorListener notify;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current(value)) return;
        if (!failure) {
            if (result) {
                phase_ = Phase::Active;
                // A long first treatment can finish behind the raw frames already on screen.
                deliver(value, std::move(result));
            } else if (phase_ == Phase::Warming) {
                phase_ = Phase::FirstFrame;
                deliver(value, value.frame);
            }
            return;
        }
        phase_ = Phase::Off;
        effectInput_.reset();
        notify = listener_;
    }
    processor_.reset();
    if (notify) { try { notify(*failure); } catch (...) {} }
    std::lock_guard<std::mutex> lock(mutex_);
    if (current(value)) deliver(value, value.frame);
}
}
