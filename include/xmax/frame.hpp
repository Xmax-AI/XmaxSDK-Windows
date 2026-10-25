//
//  frame.hpp
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

namespace xmax {

/**
 * Exclusively owns a frame handle with read-only pixel access and native window rendering.
 */
class frame : public detail::owner<xmax_frame, xmax_frame_release> {

  public:

    /**
     * Inherits the handle owner's constructors; an empty handle creates an empty owner.
     *
     * @note The underlying resource is not copied. Destruction invokes its matching release function.
     */
    using owner::owner;

    /**
     * Returns a borrowed pixel view valid until the frame owner is released.
     *
     * @see xmax_frame_get_view
     */
    xmax_video_frame view() const {
        xmax_video_frame result{};
        result.struct_size = sizeof(result);
        check(xmax_frame_get_view(
            get(),
            &result
        ));
        return result;
    }

    /**
     * Returns the horizontal display-mirror flag; pixels retain capture or decode orientation.
     *
     * @return Whether custom rendering should flip horizontally; native window rendering handles this
     * automatically.
     *
     * @see xmax_frame_get_mirrored
     */
    bool mirrored() const {
        int32_t value = 0;
        check(xmax_frame_get_mirrored(
            get(),
            &value
        ));
        return value != 0;
    }

    /**
     * Renders a frame on the target HWND's owner thread.
     *
     * @param[in] hwnd The target Windows HWND as void*, owned by the calling thread.
     * @param[in] mode XMAX_CONTENT_FIT for aspect fit, or XMAX_CONTENT_FILL for centered aspect fill.
     *
     * @see xmax_frame_render_window
     */
    void render_window(
        void* hwnd,
        int32_t mode = XMAX_CONTENT_FIT
    ) const {
        check(xmax_frame_render_window(
            get(),
            hwnd,
            mode
        ));
    }
};
} // namespace xmax
