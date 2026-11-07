//
//  XmaxCreditBalance.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once

namespace xmax {
/** Server-confirmed credits for the account owning the client's API key. */
struct XmaxCreditBalance {
    /** Finite nonnegative credits, including fractional credits. */
    double current_balance;
};
} // namespace xmax
