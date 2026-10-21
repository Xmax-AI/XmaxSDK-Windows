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
 * 以独占所有权管理 SDK 返回的 UTF-8 字符串，可复制内容到调用方内存。
 */
class string : public detail::owner<xmax_string, xmax_string_release> {

  public:

    /**
     * 继承句柄所有者的构造接口，接管传入句柄；空句柄表示空对象。
     *
     * @note 不复制底层资源，销毁时调用与句柄匹配的释放接口。
     */
    using owner::owner;

    /**
     * 将 SDK 拥有的 UTF-8 文本复制到调用方内存。
     *
     * @return 由调用方拥有的 UTF-8 文本副本，空句柄返回空字符串。
     * @see xmax_string_data
     */
    std::string value() const {
        const auto* text = xmax_string_data(get());
        return text ? text : "";
    }
};
} // namespace xmax
