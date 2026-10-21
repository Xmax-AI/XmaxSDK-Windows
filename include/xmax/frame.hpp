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
 * 以独占所有权管理视频帧句柄，提供只读像素视图和原生窗口呈现。
 */
class frame : public detail::owner<xmax_frame, xmax_frame_release> {

  public:

    /**
     * 继承句柄所有者的构造接口，接管传入句柄；空句柄表示空对象。
     *
     * @note 不复制底层资源，销毁时调用与句柄匹配的释放接口。
     */
    using owner::owner;

    /**
     * 借用帧的只读像素视图，视图在帧对象释放前有效。
     *
     * @see xmax_frame_get_view
     */
    xmax_video_frame view() const {
        xmax_video_frame result{};
        result.struct_size = sizeof(result);
        check(xmax_frame_get_view(get(), &result));
        return result;
    }

    /**
     * 在目标 HWND 的所属线程呈现视频帧。
     *
     * @param[in] hwnd 目标 Windows HWND 转换为 void* 的值，必须由当前线程拥有。
     * @param[in] mode XMAX_CONTENT_FIT 等比例适应，或 XMAX_CONTENT_FILL 居中填满。
     * @see xmax_frame_render_window
     */
    void render_window(
        void* hwnd,
        int32_t mode = XMAX_CONTENT_FIT
    ) const {
        check(xmax_frame_render_window(get(), hwnd, mode));
    }
};
} // namespace xmax
