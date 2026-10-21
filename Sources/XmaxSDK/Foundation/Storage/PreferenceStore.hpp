//
//  PreferenceStore.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Common.hpp"
#include <filesystem>
#include <optional>

namespace xmax::detail {

/**
 * 定义非凭证偏好数据的持久化接口，读取和写入失败均不得向调用方抛出异常。
 */
class PreferenceStore {

  public:

    /**
     * 在所有调用结束后释放偏好缓存后端。
     *
     * @note 销毁前应结束使用本对象的并发调用。
     */
    virtual ~PreferenceStore() = default;

    /**
     * 读取已缓存字节；缓存不存在或不可读时返回 std::nullopt。
     *
     * @param[in] key 有效的非凭证偏好缓存键，不得包含目录穿越字符。
     */
    virtual std::optional<std::string> read(
        const std::string& key
    ) noexcept = 0;

    /**
     * 以尽力而为的方式保存非凭证偏好，缓存失败不抛出异常。
     *
     * @param[in] key 有效的非凭证偏好缓存键，不得包含目录穿越字符。
     * @param[in] value 需要持久化的非凭证字节内容。
     */
    virtual void write(
        const std::string& key,
        const std::string& value
    ) noexcept = 0;
};

/**
 * 创建非凭证缓存后端；空目录参数使用当前用户的应用缓存目录。
 *
 * @param[in] directory 非凭证缓存根目录，空文本使用当前用户应用缓存目录。
 * @return 可共享的非凭证偏好后端。
 */
std::shared_ptr<PreferenceStore> make_preference_store(
    std::filesystem::path directory = {}
);
} // namespace xmax::detail
