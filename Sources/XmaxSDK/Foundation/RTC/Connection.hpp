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
 * 保存加入生成服务 RTC 房间所需的应用、账号、令牌和结果流身份。
 */
struct Connection {

    /**
     * RTC 应用标识。
     */
    std::string app_id;

    /**
     * 加入 RTC 时使用的房间标识。
     */
    std::string room;

    /**
     * 发布本地输入视频的 RTC 账号。
     */
    std::string user;

    /**
     * 加入房间使用的 RTC 临时凭证，不得写入未脱敏日志。
     */
    std::string token;

    /**
     * 生成结果服务用户的 RTC 账号。
     */
    std::string bot;
};
} // namespace xmax::detail
