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
 * The JSON value type used for service payloads and structured signaling.
 */
using json = nlohmann::json;
} // namespace xmax::detail
