//
//  Connection.hpp
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
 * The app, account, token, and result-stream identity required to join the generation RTC room.
 */
struct Connection {

    /**
     * The RTC application identifier.
     */
    std::string app_id;

    /**
     * The RTC room identifier used when joining.
     */
    std::string room;

    /**
     * The RTC account publishing local input video.
     */
    std::string user;

    /**
     * The temporary RTC room credential; never write it to unredacted logs.
     */
    std::string token;

    /**
     * The RTC account of the generation result user.
     */
    std::string bot;
};
} // namespace xmax::detail
