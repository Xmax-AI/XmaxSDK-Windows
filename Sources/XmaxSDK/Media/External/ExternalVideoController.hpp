//
//  ExternalVideoController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <mutex>

namespace xmax::detail {

/**
 * Receives frames synchronously; receivers must copy pixels needed after the callback returns.
 */
using VideoFrameSink = std::function<void(const Frame&)>;

/**
 * Manages external input binding, increasing timestamps, and publication and preview delivery.
 */
class ExternalVideoController {

  public:

    /**
     * Validates and binds an input format, resetting timestamp tracking for the binding.
     *
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] binding The source revision; zero reserves the channel for unversioned camera delivery.
     */
    void prepare(
        xmax_video_format format,
        uint64_t binding = 0
    );

    /**
     * Installs a publication receiver and starts requesting input frames.
     *
     * @param[in] sink The synchronous frame receiver, invoked on the submitting media thread.
     */
    void start(
        VideoFrameSink sink
    );

    /**
     * Stops publication delivery while retaining the input format and local preview.
     */
    void stop();

    /**
     * Releases the input binding, clearing format, publication state, and timestamp tracking.
     */
    void close();

    /**
     * Validates and copies a borrowed external frame, delivering it synchronously to publication and preview
     * receivers.
     *
     * @param[in] source The frame or borrowed view to read, with a valid pixel layout.
     * @param[in] binding The source revision; zero uses the current legacy binding. Stale revisions are ignored.
     */
    void push(
        const xmax_video_frame& source,
        uint64_t binding = 0
    );

    /**
     * Delivers camera frames at capture dimensions; RTC handles encoding dimensions and aspect ratio.
     *
     * @param[in] frame The frame or borrowed view to deliver; its pixels must remain valid until this
     * synchronous call returns.
     */
    void push_owned(
        Frame frame
    );

    /**
     * Reports whether a valid external input format is bound.
     */
    bool bound() const;

    /**
     * Reports whether the publication receiver currently requests frames without blocking.
     */
    bool requesting() const {
        return requesting_.load();
    }

    /**
     * Returns the input format under the binding lock.
     *
     * @return The bound input format; dimensions and frame rate are 0 when unbound.
     */
    xmax_video_format format() const;

    /**
     * Replaces the local preview receiver, invoked on the submitting media thread.
     *
     * @param[in] sink The synchronous frame receiver, invoked on the submitting media thread.
     * @param[in] binding A nonzero replacement revision invalidates previous sources before preview changes.
     */
    void preview(
        std::function<void(Frame)> sink,
        uint64_t binding = 0
    );

  private:

    /**
     * The mutex protecting binding, timestamps, and receivers while coordinating stop and delivery.
     */
    mutable std::mutex mutex_;

    /**
     * The external input format or camera encoding target; camera capture dimensions may differ.
     */
    xmax_video_format format_{};

    /**
     * The latest accepted monotonic timestamp for this input binding, in microseconds.
     */
    int64_t last_timestamp_ = -1;

    /**
     * The bound external source revision, checked under the delivery lock before pixels are read.
     */
    uint64_t binding_ = 0;

    /**
     * Whether this input currently delivers frames to an active RTC receiver.
     */
    std::atomic<bool> requesting_{false};

    /**
     * The publication receiver, invoked synchronously on the submitting media thread.
     */
    VideoFrameSink sink_;

    /**
     * The local preview receiver, invoked synchronously on the submitting media thread.
     */
    std::function<void(Frame)> preview_;
};
} // namespace xmax::detail
