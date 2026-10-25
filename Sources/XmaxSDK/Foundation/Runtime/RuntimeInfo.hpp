//
//  RuntimeInfo.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Json.hpp"

namespace xmax::detail {

/**
 * Builds SDK, system, and device metadata for server-session creation.
 */
json runtime_info();

/**
 * Generates a random identifier for signaling correlation and fragmentation.
 */
std::string random_id();

/**
 * Generates a task identifier in the generation signaling format.
 */
std::string task_id();
} // namespace xmax::detail
