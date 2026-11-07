//
//  storage_transfer_callbacks.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "xmax.h"

namespace xmax {

/**
 * Per-transfer callbacks for a blocking storage operation.
 *
 * The application retains the context until the operation returns.
 */
struct storage_transfer_callbacks {

    /**
     * Receives transferred and total byte counts; may be null.
     */
    xmax_storage_progress_fn progress = nullptr;

    /**
     * Returns nonzero to cancel; may be null.
     */
    xmax_cancellation_fn cancel = nullptr;

    /**
     * The application-owned context borrowed by both callbacks.
     */
    void* user_data = nullptr;
};

} // namespace xmax
