//
//  media_frame.hpp
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
 * Owns a remote PCM or video packet whose borrowed media views remain valid until release.
 */
class media_frame : public detail::owner<xmax_media_frame, xmax_media_frame_release> {

  public:

    /**
     * Inherits exclusive ownership of an owned packet handle; never adopt a callback-borrowed handle.
     */
    using owner::owner;

    /**
     * Returns borrowed PCM or video data with its shared stream identity and presentation time.
     *
     * @see xmax_media_frame_get_view
     */
    xmax_media_frame_view view() const {
        xmax_media_frame_view result{};
        result.struct_size = sizeof(result);
        check(xmax_media_frame_get_view(
            get(),
            &result
        ));
        return result;
    }

    /**
     * Creates another owner of the same immutable media without copying pixels or PCM bytes.
     *
     * @return An independent packet owner that may outlive this handle or its realtime controller.
     */
    media_frame retain() const {
        xmax_media_frame retained = nullptr;
        check(xmax_media_frame_retain(
            get(),
            &retained
        ));
        return media_frame(retained);
    }
};
} // namespace xmax
