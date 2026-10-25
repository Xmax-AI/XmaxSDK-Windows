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
 *
 * The C++17 exclusive resource-management interface built on the stable C ABI.
 *
 * Handle owners are movable but not copyable; get() borrows the raw handle. Wrappers throw xmax::error for
 * negative SDK results and retain the C interface's blocking, threading, and ownership contracts.
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
#include "media_frame.hpp"
#include "external_frame_sink.hpp"
#include "camera_device.hpp"
#include "uploaded_file_info.hpp"
#include "uploaded_file.hpp"
#include "storage.hpp"
#include "realtime.hpp"
#include "client.hpp"
