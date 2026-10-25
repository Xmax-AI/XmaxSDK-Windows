//
//  RemoteMediaFrameDispatcher.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Video/RemoteMediaFrameDispatcher.hpp"
#include "Render/Video/RemoteAudioTimeline.hpp"
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace xmax::detail {
namespace {

/**
 * Allocates result identities independently of controller and RTC UID reuse.
 */
std::atomic<uint64_t> next_stream_id{0};
} // namespace

/**
 * Owns queue accounting, listener revisions, and producer timing under a single mutex.
 */
struct RemoteMediaFrameDispatcher::State {

    /**
     * One queued packet and the listener revision admitting it.
     */
    struct Pending {

        /**
         * The listener revision that accepted the packet.
         */
        uint64_t revision;

        /**
         * The owned audio or video packet.
         */
        std::shared_ptr<const RemoteMediaFrame> frame;
    };

    /**
     * Protects all state other than immutable queue limits.
     */
    std::mutex mutex;

    /**
     * Wakes the output worker for admitted packets or shutdown.
     */
    std::condition_variable ready;

    /**
     * The current application handler, copied before invocation outside the mutex.
     */
    RemoteMediaListener listener;

    /**
     * The queue of admitted packets in producer delivery order.
     */
    std::deque<Pending> queue;

    /**
     * The maximum packets retained, including an in-flight callback.
     */
    size_t maximum_packets;

    /**
     * The maximum media bytes retained, including an in-flight callback.
     */
    size_t maximum_bytes;

    /**
     * The number of admitted packets not yet released by the worker.
     */
    size_t pending_packets = 0;

    /**
     * The retained media bytes counted for pending_packets.
     */
    size_t pending_bytes = 0;

    /**
     * The current listener and binding revision.
     */
    uint64_t revision = 0;

    /**
     * The active producer's connection revision.
     */
    uint64_t token = 0;

    /**
     * The shared result identity, or zero without an active binding.
     */
    uint64_t stream_id = 0;

    /**
     * Whether no further output should be delivered.
     */
    bool stopping = false;

    /**
     * The exact sample-count audio rendering clock.
     */
    RemoteAudioTimeline audio_timeline;

    /**
     * The preceding video timestamp, in microseconds.
     */
    std::optional<int64_t> last_video_time;

    /**
     * The preceding video width, height, and pixel layout.
     */
    std::array<uint32_t, 3> last_video_format{};

    /**
     * Whether the next admitted PCM packet must expose an output gap.
     */
    bool audio_discontinuous = true;

    /**
     * Whether the next admitted video packet must expose an output gap.
     */
    bool video_discontinuous = true;

    /**
     * Stores immutable output limits before the worker starts.
     *
     * @param[in] packets The positive packet limit.
     * @param[in] bytes The positive retained-media byte limit.
     */
    State(
        size_t packets,
        size_t bytes
    )
        : maximum_packets(packets), maximum_bytes(bytes) {
    }

    /**
     * Clears pending output and both track clocks while preserving an in-flight packet's accounting.
     */
    void reset() {
        ++revision;
        for (const auto& item : queue) {
            --pending_packets;
            pending_bytes -= item.frame->byte_size();
        }
        queue.clear();
        audio_timeline.reset();
        last_video_time.reset();
        last_video_format = {};
        audio_discontinuous = video_discontinuous = true;
    }

    /**
     * Admits a packet within both memory and packet limits; requires mutex.
     *
     * @param[in] frame The owned normalized output packet.
     *
     * @return Whether the packet was admitted; rejected output leaves its timeline position unchanged.
     */
    bool enqueue(
        std::shared_ptr<const RemoteMediaFrame> frame
    ) {
        const auto bytes = frame->byte_size();
        if (pending_packets >= maximum_packets || bytes > maximum_bytes - pending_bytes)
            return false;
        queue.push_back({revision, std::move(frame)});
        ++pending_packets;
        pending_bytes += bytes;
        ready.notify_one();
        return true;
    }

    /**
     * Delivers packets serially without holding admission locks while application code executes.
     */
    void run() {
        for (;;) {
            Pending item{};
            RemoteMediaListener callback;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(
                    lock,
                    [this] { return stopping || !queue.empty(); }
                );
                if (stopping)
                    return;
                item = std::move(queue.front());
                queue.pop_front();
                if (item.revision == revision && item.frame->stream_id == stream_id)
                    callback = listener;
            }

            if (callback) {
                try {
                    callback(item.frame);
                } catch (...) {
                    // Application failures must not terminate the media delivery worker.
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                --pending_packets;
                pending_bytes -= item.frame->byte_size();
            }
        }
    }
};

/**
 * Starts a serial output worker with limits covering queued packets and an in-flight callback.
 *
 * @param[in] maximum_packets The positive retained packet limit, normally 256.
 * @param[in] maximum_bytes The positive retained PCM and pixel byte limit, normally 32 MiB.
 */
RemoteMediaFrameDispatcher::RemoteMediaFrameDispatcher(
    size_t maximum_packets,
    size_t maximum_bytes
) {
    require(
        maximum_packets > 0 && maximum_bytes > 0,
        XMAX_INVALID_ARGUMENT,
        "Media output queue limits must be positive"
    );
    state_ = std::make_shared<State>(
        maximum_packets,
        maximum_bytes
    );
    worker_ = std::thread([state = state_] { state->run(); });
}

/**
 * Discards pending output and finishes an in-flight callback before joining the delivery worker.
 */
RemoteMediaFrameDispatcher::~RemoteMediaFrameDispatcher() {
    shutdown();
}

/**
 * Stops admission, drops queued packets, and drains delivery before lifecycle execution is shut down.
 * Safe to repeat; a shutdown requested by the delivery callback lets that invocation finish normally.
 */
void RemoteMediaFrameDispatcher::shutdown() {
    RemoteMediaListener previous;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stopping = true;
        state_->reset();
        previous = std::move(state_->listener);
    }
    state_->ready.notify_all();
    if (!worker_.joinable())
        return;
    if (worker_.get_id() == std::this_thread::get_id())
        worker_.detach();
    else
        worker_.join();
}

/**
 * Replaces the listener and discards its queued output; an in-flight callback may finish afterward.
 *
 * @param[in] listener The owned-packet callback, or an empty function to stop application delivery.
 *
 * @return The replaced callback, allowing its context to be released outside lifecycle execution.
 */
RemoteMediaListener RemoteMediaFrameDispatcher::listener(
    RemoteMediaListener listener
) {
    RemoteMediaListener previous;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        require(
            !state_->stopping,
            XMAX_INVALID_STATE,
            "Remote media output is shutting down"
        );
        previous = std::move(state_->listener);
        state_->listener = std::move(listener);
        state_->reset();
    }
    return previous;
}

/**
 * Reports whether an application listener is registered.
 */
bool RemoteMediaFrameDispatcher::has_listener() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return bool(state_->listener);
}

/**
 * Creates a fresh result identity shared by both tracks and rejects previous producers.
 *
 * @param[in] token The connection revision identifying current RTC callbacks.
 */
void RemoteMediaFrameDispatcher::bind(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (token < state_->token)
        return;
    state_->reset();
    state_->token = token;
    state_->stream_id = ++next_stream_id;
}

/**
 * Disables output and drops queued packets while retaining the application listener.
 *
 * @param[in] token The invalidating connection revision; older revisions cannot unbind newer output.
 */
void RemoteMediaFrameDispatcher::unbind(
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (token < state_->token)
        return;
    state_->token = token;
    state_->stream_id = 0;
    state_->reset();
}

/**
 * Resolves sample timing and admits a current producer's copied PCM packet without waiting for callbacks.
 *
 * @param[in] frame The owned PCM packet and native receiver timestamp.
 * @param[in] token The producer's connection revision; stale producers are ignored.
 */
void RemoteMediaFrameDispatcher::audio(
    AudioFrame frame,
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->stream_id || state_->token != token || !state_->listener || state_->stopping)
        return;

    auto packet = std::make_shared<RemoteMediaFrame>();
    bool discontinuous = false;
    if (!state_->audio_timeline.resolve(
        frame,
        packet->presentation_time,
        discontinuous
    )) {
        state_->audio_discontinuous = true;
        return;
    }
    packet->type = XMAX_MEDIA_AUDIO;
    packet->stream_id = state_->stream_id;
    packet->discontinuous = state_->audio_discontinuous || discontinuous;
    packet->duration = {frame.samples_per_channel, int32_t(frame.sample_rate)};
    packet->audio = std::move(frame);
    state_->audio_discontinuous = !state_->enqueue(std::move(packet));
}

/**
 * Admits a final decoded frame, marking format changes, backward times, and output loss.
 *
 * @param[in] frame The owned read-only pixels with a microsecond rendering timestamp.
 * @param[in] token The producer's connection revision; stale producers are ignored.
 */
void RemoteMediaFrameDispatcher::video(
    std::shared_ptr<Frame> frame,
    uint64_t token
) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->stream_id || state_->token != token || !state_->listener || state_->stopping)
        return;
    if (!frame || frame->timestamp_us < 0) {
        state_->video_discontinuous = true;
        return;
    }

    const std::array<uint32_t, 3> format{frame->width, frame->height, uint32_t(frame->format)};
    auto packet = std::make_shared<RemoteMediaFrame>();
    packet->type = XMAX_MEDIA_VIDEO;
    packet->stream_id = state_->stream_id;
    packet->discontinuous = state_->video_discontinuous ||
                            (state_->last_video_time && frame->timestamp_us <= *state_->last_video_time) ||
                            (state_->last_video_time && format != state_->last_video_format);
    packet->presentation_time = {frame->timestamp_us, 1000000};
    state_->last_video_time = frame->timestamp_us;
    state_->last_video_format = format;
    packet->video = std::move(frame);
    state_->video_discontinuous = !state_->enqueue(std::move(packet));
}
} // namespace xmax::detail
