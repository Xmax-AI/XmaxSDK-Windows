//
//  string.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "handle_owner.hpp"
#include <string>

namespace xmax {

/**
 * Exclusively owns SDK-returned UTF-8 text and supports copying it into app-owned storage.
 */
class string : public detail::owner<xmax_string, xmax_string_release> {

  public:

    /**
     * Inherits the handle owner's constructors; an empty handle creates an empty owner.
     *
     * @note The underlying resource is not copied. Destruction invokes its matching release function.
     */
    using owner::owner;

    /**
     * Copies SDK-owned UTF-8 text into app-owned storage.
     *
     * @return An app-owned UTF-8 copy, or an empty string for an empty handle.
     *
     * @see xmax_string_data
     */
    std::string value() const {
        const auto* text = xmax_string_data(get());
        return text ? text : "";
    }
};
} // namespace xmax
