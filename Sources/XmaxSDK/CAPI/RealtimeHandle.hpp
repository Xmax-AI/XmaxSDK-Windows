//
//  RealtimeHandle.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Core/Realtime/XmaxRealtimeManager.hpp"

/**
 * Owns a realtime controller; released by xmax_realtime_destroy.
 */
struct xmax_realtime_impl {

    /**
     * The controller retained by this handle and by active source-receiver calls.
     */
    std::shared_ptr<xmax::detail::XmaxRealtimeManager> session;

    /**
     * Takes ownership of a realtime controller for this handle.
     *
     * @param[in] value The realtime controller whose exclusive ownership is transferred to this handle.
     */
    explicit xmax_realtime_impl(
        std::unique_ptr<xmax::detail::XmaxRealtimeManager> value
    )
        : session(std::move(value)) {
    }
};

/**
 * Retains a remote packet's owned media; callback handles borrow a stack instance of this wrapper.
 */
struct xmax_media_frame_impl {

    /**
     * The immutable audio or video packet shared by retained handles.
     */
    std::shared_ptr<const xmax::detail::RemoteMediaFrame> frame;
};

/**
 * Refers weakly to one external source binding without retaining its controller.
 */
struct xmax_external_frame_sink_impl {

    /**
     * The controller accepting this binding; expired controllers silently discard input.
     */
    std::weak_ptr<xmax::detail::XmaxRealtimeManager> session;

    /**
     * The binding revision rejecting late callbacks after source replacement.
     */
    uint64_t binding;

    /**
     * The validated format retained at binding creation.
     */
    xmax_video_format format;
};
