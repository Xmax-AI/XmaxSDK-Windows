//
//  downloaded_file.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <cstdint>
#include <string>

namespace xmax {

/**
 * Identifies the local file published by a successful download.
 */
struct downloaded_file {

    /**
     * The UTF-8 destination path.
     */
    std::string path;

    /**
     * The number of downloaded bytes.
     */
    uint64_t bytes = 0;
};

} // namespace xmax
