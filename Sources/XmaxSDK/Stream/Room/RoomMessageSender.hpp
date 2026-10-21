//
//  RoomMessageSender.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/RTC/RtcPlatform.hpp"
#include "Stream/Room/MessageCodec.hpp"
#include <deque>

namespace xmax::detail {

/**
 * 将房间信令分片并按滑动发送预算投递，等待期间支持取消及保活。
 */
class RoomMessageSender {

  public:

    /**
     * 按滑动预算发送 UTF-8 信令分片，等待期间检查取消并执行保活。
     *
     * @param[in] rtc 已连接且可发送信令分片的 RTC 适配器借用引用。
     * @param[in] message 待序列化并分片发送的房间 JSON 信令。
     * @param[in] cancelled 协作式取消判断函数；返回 true 时应终止当前操作。
     * @param[in] maintain 等待或限速期间执行的维护回调，空函数表示无需额外维护。
     */
    void
    send(
        Rtc& rtc,
        const json& message,
        const Cancel& cancelled,
        const std::function<void()>& maintain = {}
    );

    /**
     * 清空房间释放后的发送预算历史。
     */
    void reset() {
        sent_.clear();
    }

  private:

    /**
     * 用于计算滑动信令发送预算的历史发送时间与大小。
     */
    std::deque<std::pair<Clock::time_point, size_t>> sent_;
};
} // namespace xmax::detail
