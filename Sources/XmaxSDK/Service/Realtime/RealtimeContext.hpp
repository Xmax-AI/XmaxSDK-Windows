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
 * Generation conditions reusable after camera switching or reconnection.
 */
struct RealtimeContext {

    /**
     * The generation prompt, encoded as UTF-8.
     */
    std::string prompt;

    /**
     * The reference image's service URL, or empty to omit the reference.
     */
    std::string reference;

    /**
     * Whether to enable face restoration.
     */
    bool face = false;
};
} // namespace xmax::detail
