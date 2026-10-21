//
//  RealtimeContext.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"

namespace xmax::detail {

/**
 * 可在摄像头切换或重新连接后复用的生成条件。
 */
struct RealtimeContext {

    /**
     * UTF-8 生成提示词。
     */
    std::string prompt;

    /**
     * 参考图片的服务地址，空文本表示不使用参考图。
     */
    std::string reference;

    /**
     * 是否启用人脸修复。
     */
    bool face = false;
};
} // namespace xmax::detail
