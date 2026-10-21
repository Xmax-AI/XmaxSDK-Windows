//
//  Json.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <nlohmann/json.hpp>

namespace xmax::detail {

/**
 * SDK 内部使用的 JSON 值类型，负责服务负载和信令的结构化处理。
 */
using json = nlohmann::json;
} // namespace xmax::detail
