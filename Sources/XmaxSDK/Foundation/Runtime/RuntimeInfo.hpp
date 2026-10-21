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
 * 构造创建服务端会话所需的 SDK、系统与设备元数据。
 */
json runtime_info();

/**
 * 生成用于信令关联和分片传输的随机标识。
 */
std::string random_id();

/**
 * 生成符合生成信令格式的任务标识。
 */
std::string task_id();
} // namespace xmax::detail
