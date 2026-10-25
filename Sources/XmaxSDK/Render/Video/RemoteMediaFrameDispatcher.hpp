//
//  RemoteMediaFrameDispatcher.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/RemoteMediaFrame.hpp"
#include <thread>

namespace xmax::detail {

/**
 * Delivers bounded, owned remote audio and video on one serial worker without blocking RTC producers.
 */
class RemoteMediaFrameDispatcher {

  public:

    /**
     * Starts a serial output worker with limits covering queued packets and an in-flight callback.
     *
     * @param[in] maximum_packets The positive retained packet limit, normally 256.
     * @param[in] maximum_bytes The positive retained PCM and pixel byte limit, normally 32 MiB.
     */
    explicit RemoteMediaFrameDispatcher(
        size_t maximum_packets = 256,
        size_t maximum_bytes = 32 * 1024 * 1024
    );

    /**
     * Discards pending output and finishes an in-flight callback before joining the delivery worker.
     */
    ~RemoteMediaFrameDispatcher();

    /**
     * Stops admission, drops queued packets, and drains delivery before lifecycle execution is shut down.
     * Safe to repeat; a shutdown requested by the delivery callback lets that invocation finish normally.
     */
    void shutdown();

    /**
     * Replaces the listener and discards its queued output; an in-flight callback may finish afterward.
     *
     * @param[in] listener The owned-packet callback, or an empty function to stop application delivery.
     *
     * @return The replaced callback, allowing its context to be released outside lifecycle execution.
     */
    RemoteMediaListener listener(
        RemoteMediaListener listener
    );

    /**
     * Reports whether an application listener is registered.
     */
    bool has_listener() const;

    /**
     * Creates a fresh result identity shared by both tracks and rejects previous producers.
     *
     * @param[in] token The connection revision identifying current RTC callbacks.
     */
    void bind(
        uint64_t token
    );

    /**
     * Disables output and drops queued packets while retaining the application listener.
     *
     * @param[in] token The invalidating connection revision; older revisions cannot unbind newer output.
     */
    void unbind(
        uint64_t token
    );

    /**
     * Resolves sample timing and admits a current producer's copied PCM packet without waiting for callbacks.
     *
     * @param[in] frame The owned PCM packet and native receiver timestamp.
     * @param[in] token The producer's connection revision; stale producers are ignored.
     */
    void audio(
        AudioFrame frame,
        uint64_t token
    );

    /**
     * Admits a final decoded frame, marking format changes, backward times, and output loss.
     *
     * @param[in] frame The owned read-only pixels with a microsecond rendering timestamp.
     * @param[in] token The producer's connection revision; stale producers are ignored.
     */
    void video(
        std::shared_ptr<Frame> frame,
        uint64_t token
    );

  private:

    /**
     * The shared queue state retained by the delivery worker until it exits.
     */
    struct State;

    /**
     * The queue, listener, revision, and timeline state.
     */
    std::shared_ptr<State> state_;

    /**
     * The serial application callback worker; it never owns RTC or lifecycle locks.
     */
    std::thread worker_;
};
} // namespace xmax::detail
