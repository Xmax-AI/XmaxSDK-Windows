//
//  RemoteVideoInterpolationPipeline.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Interpolation/RemoteVideoInterpolationPipeline.hpp"
#include "Foundation/Logging/XmaxLogger.hpp"
#include <algorithm>
#include <cstring>

namespace xmax::detail {
namespace {
size_t frame_bytes(const Frame& frame) {
    return frame.planes[0].size() + frame.planes[1].size() + frame.planes[2].size();
}
}

RemoteVideoInterpolationPipeline::RemoteVideoInterpolationPipeline(Output output, InterpolationProcessorFactory factory)
    : output_(std::move(output)), factory_(std::move(factory)),
      inference_([this] { infer(); }), presenter_([this] { present(); }) {
    status_.struct_size = sizeof(status_);
}

RemoteVideoInterpolationPipeline::~RemoteVideoInterpolationPipeline() { shutdown(); }

void RemoteVideoInterpolationPipeline::clear_locked() {
    ++revision_;
    previous_.reset();
    pending_.reset();
    presentations_.clear();
    queued_bytes_ = 0;
}

void RemoteVideoInterpolationPipeline::configure(std::optional<InterpolationOptions> options) {
    if (options) validate_interpolation(*options);
    require(inference_.get_id() != std::this_thread::get_id(), XMAX_INVALID_STATE,
            "Cannot configure interpolation from its inference thread");
    auto command = std::make_shared<std::packaged_task<void()>>([this, options] {
        auto processor = options ? factory_(*options) : nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        clear_locked();
        processor_.swap(processor);
        buffer_ms_ = options ? options->buffer_ms : 125;
        status_ = {};
        status_.struct_size = sizeof(status_);
        status_.enabled = options.has_value();
        wake_.notify_all();
    });
    auto result = command->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        require(!stopped_, XMAX_INVALID_STATE, "Interpolation has shut down");
        clear_locked();
        commands_.push_back([command] { (*command)(); });
    }
    wake_.notify_all();
    result.get();
}

xmax_video_interpolation_status RemoteVideoInterpolationPipeline::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RemoteVideoInterpolationPipeline::bind(uint64_t token, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (token < token_) return;
    token_ = token;
    active_ = active;
    clear_locked();
    wake_.notify_all();
}

bool RemoteVideoInterpolationPipeline::schedule_locked(std::shared_ptr<Frame> frame,
                                                       Clock::time_point due, bool intermediate) {
    constexpr size_t maximum_bytes = 64 * 1024 * 1024;
    const auto bytes = frame_bytes(*frame);
    if (presentations_.size() >= 8 || bytes > maximum_bytes - queued_bytes_) {
        ++status_.skipped_frames;
        return false;
    }
    const auto at = std::upper_bound(presentations_.begin(), presentations_.end(), due,
        [](Clock::time_point time, const Presentation& item) { return time < item.due; });
    presentations_.insert(at, {std::move(frame), due, intermediate});
    queued_bytes_ += bytes;
    return true;
}

void RemoteVideoInterpolationPipeline::submit(std::shared_ptr<Frame> frame, uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || !active_ || token != token_) return;
    if (!status_.enabled) {
        output_(std::move(frame), token);
        return;
    }
    ++status_.input_frames;
    if (frame_bytes(*frame) > 64 * 1024 * 1024) {
        clear_locked();
        ++status_.skipped_frames;
        output_(std::move(frame), token);
        return;
    }
    const auto now = Clock::now();
    const auto delay = std::chrono::milliseconds(buffer_ms_);
    // Restart the timeline on a cut in timestamps or geometry, or after an arrival gap.
    if (previous_ && (frame->width != previous_->width || frame->height != previous_->height ||
        frame->timestamp_us <= previous_->timestamp_us ||
        frame->timestamp_us - previous_->timestamp_us > 250000 ||
        now > anchor_ + std::chrono::microseconds(frame->timestamp_us - first_timestamp_) + delay)) {
        clear_locked();
        ++status_.skipped_frames;
    }
    if (!previous_) {
        anchor_ = now + delay;
        first_timestamp_ = frame->timestamp_us;
    }
    const auto due = anchor_ + std::chrono::microseconds(frame->timestamp_us - first_timestamp_);
    if (!schedule_locked(frame, due, false)) {
        // Queue pressure returns immediately to the most recent original frame.
        clear_locked();
        anchor_ = now + delay;
        first_timestamp_ = frame->timestamp_us;
        schedule_locked(frame, anchor_, false);
    }
    if (previous_) {
        if (pending_) ++status_.skipped_frames;
        const auto midpoint = previous_->timestamp_us + (frame->timestamp_us - previous_->timestamp_us) / 2;
        pending_ = Pair{previous_, frame, token, revision_,
            anchor_ + std::chrono::microseconds(midpoint - first_timestamp_)};
    }
    previous_ = std::move(frame);
    wake_.notify_all();
}

bool RemoteVideoInterpolationPipeline::current_locked(const Pair& pair) const {
    return !stopped_ && active_ && status_.enabled && pair.token == token_ && pair.revision == revision_;
}

void RemoteVideoInterpolationPipeline::process(Pair pair) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_locked(pair)) return;
        if (Clock::now() >= pair.deadline) { ++status_.skipped_frames; return; }
    }
    try {
        auto frame = processor_->midpoint(pair.previous, pair.next);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_locked(pair)) return;
        if (!frame || Clock::now() >= pair.deadline) ++status_.skipped_frames;
        else schedule_locked(std::move(frame), pair.deadline, true);
    } catch (const std::exception& error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!current_locked(pair)) return;
            auto latest = previous_;
            clear_locked();
            status_.enabled = false;
            status_.error_code = XMAX_MEDIA_ERROR;
            const auto message = XmaxLogger::redact(error.what());
            std::memcpy(status_.error_message, message.data(), std::min(message.size(), sizeof(status_.error_message) - 1));
            output_(std::move(latest), pair.token);
        }
        processor_.reset();
    }
    wake_.notify_all();
}

void RemoteVideoInterpolationPipeline::infer() {
    for (;;) {
        std::function<void()> command;
        std::optional<Pair> pair;
        std::optional<uint64_t> reset_revision;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                return stopped_ || !commands_.empty() || pending_ ||
                    (processor_ && processor_revision_ != revision_);
            });
            if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
            else if (stopped_) break;
            else if (processor_ && processor_revision_ != revision_) reset_revision = revision_;
            else pair = std::exchange(pending_, {});
        }
        if (command) command();
        else if (reset_revision) {
            processor_->reset();
            processor_revision_ = *reset_revision;
        }
        else if (pair) process(std::move(*pair));
    }
    processor_.reset();
}

void RemoteVideoInterpolationPipeline::present() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopped_) {
        if (presentations_.empty()) {
            wake_.wait(lock, [this] { return stopped_ || !presentations_.empty(); });
            continue;
        }
        const auto due = presentations_.front().due;
        if (Clock::now() < due) { wake_.wait_until(lock, due); continue; }
        auto item = std::move(presentations_.front());
        presentations_.pop_front();
        queued_bytes_ -= frame_bytes(*item.frame);
        // A delayed presenter keeps the newest due frame so a pause cannot create a playback burst.
        if (!presentations_.empty() && presentations_.front().due <= Clock::now()) {
            if (item.intermediate) ++status_.skipped_frames;
            continue;
        }
        if (item.intermediate) ++status_.interpolated_frames;
        output_(std::move(item.frame), token_);
    }
}

void RemoteVideoInterpolationPipeline::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        active_ = false;
        clear_locked();
    }
    wake_.notify_all();
    if (inference_.joinable()) inference_.join();
    if (presenter_.joinable()) presenter_.join();
}

} // namespace xmax::detail
