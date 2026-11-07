//
//  realtime_context.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once

#include <optional>
#include <string>

namespace xmax {

/** Text and optional uploaded reference path for a generation request. */
struct realtime_context {
    std::string prompt;
    std::optional<std::string> reference_path;
};

} // namespace xmax
