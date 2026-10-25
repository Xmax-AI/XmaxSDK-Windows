//
//  external_frame_sink.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "handle_owner.hpp"
#include "error.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace xmax {

/**
 * Submits video and unrecoverable errors for one external source binding without retaining its controller.
 * Source capture, permissions, pacing, and final resource release remain your app's responsibility.
 */
class external_frame_sink : public detail::owner<xmax_external_frame_sink, xmax_external_frame_sink_release> {

  public:

    /**
     * Inherits ownership of a source receiver; releasing it does not unbind or stop caller-owned capture.
     */
    using owner::owner;

    /**
     * Returns the exact input format retained when this source was bound.
     */
    xmax_video_format format() const {
        xmax_video_format result{};
        check(xmax_external_frame_sink_get_format(
            get(),
            &result
        ));
        return result;
    }

    /**
     * Reports publication demand for this source; stale or closed bindings return false.
     */
    bool requesting() const {
        int32_t result = 0;
        check(xmax_external_frame_sink_is_requesting_frames(
            get(),
            &result
        ));
        return result != 0;
    }

    /**
     * Copies a current binding's pixels before returning; stale source callbacks are ignored.
     *
     * @param[in] frame The borrowed BGRA, NV12, or I420 pixels matching format(), with increasing monotonic
     * microsecond timestamps. Do not mutate pixels during submission.
     */
    void push(
        const xmax_video_frame& frame
    ) const {
        check(xmax_external_frame_sink_push_video_frame(
            get(),
            &frame
        ));
    }

    /**
     * Interrupts work using this failed source and schedules media and connection cleanup.
     *
     * @param[in] code A negative SDK error code, normally XMAX_MEDIA_ERROR.
     * @param[in] message The UTF-8 failure description, copied and redacted before application delivery.
     * @param[in] api_code The service error code when present, otherwise zero.
     * @param[in] http_status The HTTP response status when present, otherwise zero.
     *
     * @note Stale and duplicate reports are ignored. Observe cleanup completion through realtime events.
     */
    void report_error(
        xmax_result code,
        const std::string& message,
        int32_t api_code = 0,
        int32_t http_status = 0
    ) const {
        xmax_error_info error{};
        error.struct_size = sizeof(error);
        error.code = code;
        error.api_code = api_code;
        error.http_status = http_status;
        auto size = std::min(message.size(), sizeof(error.message) - 1);
        while (size && size < message.size() && (static_cast<unsigned char>(message[size]) & 0xc0) == 0x80)
            --size;
        std::memcpy(
            error.message,
            message.data(),
            size
        );
        check(xmax_external_frame_sink_report_error(
            get(),
            &error
        ));
    }
};
} // namespace xmax
