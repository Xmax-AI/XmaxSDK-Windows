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
 * Exclusively owns a C handle and releases it through its matching SDK function; movable but not copyable.
 *
 * @tparam T The handle or interface type managed by this owner.
 * @tparam Release The matching SDK release function, which must accept an empty handle.
 */
template <class T, void(XMAX_CALL* Release)(T)>
class owner {

  public:

    /**
     * Takes ownership of a possibly empty C handle and manages it through the matching release function.
     */
    explicit owner(
        T value = nullptr
    ) noexcept
        : value_(value) {
    }

    /**
     * Destroys the handle using its matching SDK release function.
     *
     * @note Finish concurrent calls using this object before destroying it.
     */
    ~owner() {
        Release(value_);
    }

    /**
     * Disables copying to prevent multiple exclusive owners from releasing the same resource.
     *
     * @param[in] other The copy source. This operation is deleted and cannot be called.
     */
    owner(
        const owner& other
    ) = delete;

    /**
     * Disables copy assignment to prevent duplicate ownership and release.
     *
     * @param[in] other The copy source. This operation is deleted and cannot be called.
     */
    owner& operator=(
        const owner& other
    ) = delete;

    /**
     * Transfers resource ownership and leaves the source empty.
     *
     * @param[in,out] other The ownership source, left empty after the move.
     */
    owner(
        owner&& other
    ) noexcept
        : value_(std::exchange(
            other.value_,
            nullptr
        )) {
    }

    /**
     * Releases the current handle and moves ownership from the source, leaving it empty.
     *
     * @param[in,out] other The ownership source, left empty after the move.
     *
     * @return A reference to this handle owner.
     */
    owner& operator=(
        owner&& other
    ) noexcept {
        if (this != &other) {
            Release(value_);
            value_ = std::exchange(
                other.value_,
                nullptr
            );
        }
        return *this;
    }

    /**
     * Returns a borrowed C handle without transferring ownership.
     *
     * @return The borrowed C handle, or nullptr for an empty owner.
     */
    T get() const noexcept {
        return value_;
    }

    /**
     * Reports whether this owner holds a nonempty handle.
     *
     * @return True for a nonempty handle; otherwise false.
     */
    explicit operator bool() const noexcept {
        return value_ != nullptr;
    }

  protected:

    /**
     * The exclusively owned C handle; the source becomes empty after a move and the matching release
     * function destroys the handle.
     */
    T value_;
};
} // namespace detail
} // namespace xmax
