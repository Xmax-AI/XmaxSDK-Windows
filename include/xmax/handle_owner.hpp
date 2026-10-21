//
//  handle_owner.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "xmax.h"
#include <utility>

namespace xmax {

namespace detail {

/**
 * 独占管理一个 C 句柄，通过匹配的 SDK 释放函数销毁资源；禁止复制，允许移动。
 *
 * @tparam T 当前所有者管理的句柄或接口类型。
 * @tparam Release 与句柄类型匹配的 SDK 释放函数，必须接受空句柄。
 */
template <class T, void(XMAX_CALL* Release)(T)> class owner {

  public:

    /**
     * 接管可为空的 C 句柄，并使用匹配释放函数管理其生命周期。
     */
    explicit owner(
        T value = nullptr
    ) noexcept
        : value_(value) {
    }

    /**
     * 使用匹配的 SDK 释放函数销毁当前句柄。
     *
     * @note 销毁前应结束使用本对象的并发调用。
     */
    ~owner() {
        Release(value_);
    }

    /**
     * 禁止复制构造，避免同一资源被多个独占所有者重复释放。
     *
     * @param[in] other 复制源对象；此操作被禁止，不得调用。
     */
    owner(
        const owner& other
    ) = delete;

    /**
     * 禁止复制赋值，避免同一资源被多个独占所有者重复释放。
     *
     * @param[in] other 复制源对象；此操作被禁止，不得调用。
     */
    owner& operator=(
        const owner& other
    ) = delete;

    /**
     * 接管源对象的资源所有权，移动后源对象为空。
     *
     * @param[in,out] other 转移资源所有权的源对象；移动完成后源对象为空。
     */
    owner(
        owner&& other
    ) noexcept
        : value_(std::exchange(other.value_, nullptr)) {
    }

    /**
     * 释放当前句柄并接管源对象，源对象在移动后为空。
     *
     * @param[in,out] other 转移资源所有权的源对象；移动完成后源对象为空。
     * @return 当前句柄所有者的引用。
     */
    owner& operator=(
        owner&& other
    ) noexcept {
        if (this != &other) {
            Release(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    /**
     * 借用当前 C 句柄，不转移所有权。
     *
     * @return 借用的 C 句柄；空所有者返回 nullptr。
     */
    T get() const noexcept {
        return value_;
    }

    /**
     * 检查当前对象是否持有非空句柄。
     *
     * @return 持有非空句柄时为 true，否则为 false。
     */
    explicit operator bool() const noexcept {
        return value_ != nullptr;
    }

  protected:

    /**
     * 独占拥有的 C 句柄；移动后源对象置空，由匹配释放函数销毁。
     */
    T value_;
};
} // namespace detail
} // namespace xmax
