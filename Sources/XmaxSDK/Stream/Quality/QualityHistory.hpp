//
//  QualityHistory.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/Storage/PreferenceStore.hpp"

namespace xmax::detail {

/**
 * 按模型与传输方向持久化最近有效档位，缓存失效时使用默认档位。
 */
class QualityHistory {

    /**
     * 缓存所属的模型标识。
     */
    std::string model_;

    /**
     * 共享的非凭证偏好持久化后端。
     */
    std::shared_ptr<PreferenceStore> store_;

    /**
     * 构造按模型与传输方向区分的非凭证偏好键。
     *
     * @param[in] downlink true 表示下行，false 表示上行。
     */
    std::string key(
        bool downlink
    ) const {
        return "quality-agora-" + model_ + (downlink ? "-downlink" : "-uplink");
    }

  public:

    /**
     * 保存模型标识与可选偏好后端，用于隔离双向画质历史。
     */
    QualityHistory(
        std::string model,
        std::shared_ptr<PreferenceStore> store
    )
        : model_(std::move(model)), store_(std::move(store)) {
    }

    /**
     * 读取画质缓存过期判断使用的 Unix UTC 秒数。
     */
    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch()
        )
            .count();
    }

    /**
     * 读取一小时内的有效画质档位；缓存缺失、失效或无效时使用档位 2。
     *
     * @param[in] downlink true 表示下行，false 表示上行。
     * @param[in] time Unix 秒时间；缺省值由当前时钟提供，用于缓存有效期判断。
     * @return 范围为 [1, 5] 的有效档位，缓存不可用时返回 2。
     */
    int read(
        bool downlink,
        int64_t time = now()
    ) const noexcept {
        try {
            if (!store_)
                return 2;

            const auto data = store_->read(key(downlink));

            if (!data)
                return 2;

            const auto record = json::parse(*data);

            if (!record.is_object() || !record.contains("level") || !record["level"].is_number_integer() ||
                !record.contains("time") || !record["time"].is_number_integer())
                return 2;

            const auto level = record["level"].get<int64_t>(), saved = record["time"].get<int64_t>();

            return level >= 1 && level <= 5 && saved > 0 && time >= saved && time - saved < 3600 ? int(level)
                                                                                                 : 2;
        } catch (...) {
            return 2;
        }
    }

    /**
     * 保存有效的方向画质档位，缓存失败不影响媒体操作。
     *
     * @param[in] level 画质档位，范围为 [1, 5]。
     * @param[in] downlink true 表示下行，false 表示上行。
     * @param[in] time Unix 秒时间；缺省值由当前时钟提供，用于缓存有效期判断。
     */
    void write(
        int level,
        bool downlink,
        int64_t time = now()
    ) const noexcept {
        try {
            if (store_ && level >= 1 && level <= 5 && time > 0)
                store_->write(key(downlink), json{{"level", level}, {"time", time}}.dump());
        } catch (...) {
        } // Cache failures must never fail a successfully applied quality change.
    }
};
} // namespace xmax::detail
