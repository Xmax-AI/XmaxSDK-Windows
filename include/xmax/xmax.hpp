//
//  xmax.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

/**
 * @file xmax.hpp
 * 提供基于稳定 C ABI 的 C++17 独占资源管理接口。
 *
 * 句柄所有者不可复制，可通过移动转移资源；get() 仅借用原始句柄。
 * 包装接口对负数 SDK 结果抛出 xmax::error；阻塞、线程和所有权约束沿用对应 C 接口。
 */

#pragma once
#include "xmax.h"
#include "RealtimeModel.hpp"
#include "XmaxEnvironment.hpp"
#include "RealtimeQualityLevel.hpp"
#include "RealtimeCameraPreset.hpp"
#include "XmaxConfiguration.hpp"
#include "RealtimeConfiguration.hpp"
#include "error.hpp"
#include "logger.hpp"
#include "string.hpp"
#include "frame.hpp"
#include "camera_device.hpp"
#include "uploaded_file_info.hpp"
#include "uploaded_file.hpp"
#include "storage.hpp"
#include "realtime.hpp"
#include "client.hpp"
