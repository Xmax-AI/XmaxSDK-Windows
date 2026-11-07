//
//  CreditBalanceService.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Service/Network/ApiService.hpp"
#include <cmath>

namespace xmax::detail {
/** Validates server-confirmed account credits independently of realtime session policy. */
class CreditBalanceService {
  public:

    static double read(
        const ApiService& api,
        const Cancel& cancelled
    ) {
        require(!cancelled(), XMAX_CANCELLED, "Credit balance query cancelled");
        auto config = api.config();
        const auto first = config.key.find_first_not_of(" \t\r\n\v\f");
        config.key = first == std::string::npos
                         ? std::string()
                         : config.key.substr(first, config.key.find_last_not_of(" \t\r\n\v\f") - first + 1);
        require(!config.key.empty(), XMAX_INVALID_API_KEY, "An Xmax API Key is required");
        ApiService account(std::move(config), api.transport());
        int32_t status = 0;
        const auto data = account.request("GET", "/balance", nullptr, cancelled, false, &status);
        require(!cancelled(), XMAX_CANCELLED, "Credit balance query cancelled");
        if (!data.is_object() || !data.contains("currentBalance") || !data["currentBalance"].is_number())
            throw Error(XMAX_API_ERROR, "Credit balance must be a finite, nonnegative number", 0, status);
        const double value = data["currentBalance"].get<double>();
        if (!std::isfinite(value) || value < 0)
            throw Error(XMAX_API_ERROR, "Credit balance must be a finite, nonnegative number", 0, status);
        return value;
    }
};
} // namespace xmax::detail
