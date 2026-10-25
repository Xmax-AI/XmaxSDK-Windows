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
 * Persists valid levels by model and direction, falling back to defaults when cache entries expire.
 */
class QualityHistory {

    /**
     * The model identifier owning the cache entry.
     */
    std::string model_;

    /**
     * The shared noncredential preference backend.
     */
    std::shared_ptr<PreferenceStore> store_;

    /**
     * Builds a noncredential preference key scoped by model and direction.
     *
     * @param[in] downlink True for downlink; false for uplink.
     */
    std::string key(
        bool downlink
    ) const {
        return "quality-agora-" + model_ + (downlink ? "-downlink" : "-uplink");
    }

  public:

    /**
     * Retains model identity and an optional preference backend for independent directional history.
     */
    QualityHistory(
        std::string model,
        std::shared_ptr<PreferenceStore> store
    )
        : model_(std::move(model)), store_(std::move(store)) {
    }

    /**
     * Returns Unix UTC seconds for quality-history expiry checks.
     */
    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch()
        )
            .count();
    }

    /**
     * Returns a valid cached level from the last hour, or level 2 for missing, expired, or invalid entries.
     *
     * @param[in] downlink True for downlink; false for uplink.
     * @param[in] time Unix time in seconds, defaulting to the current clock, for cache expiry.
     *
     * @return A valid level in [1, 5], or 2 when cached history is unavailable.
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
     * Persists a valid directional level without allowing cache failure to affect media operations.
     *
     * @param[in] level The quality level, in [1, 5].
     * @param[in] downlink True for downlink; false for uplink.
     * @param[in] time Unix time in seconds, defaulting to the current clock, for cache expiry.
     */
    void write(
        int level,
        bool downlink,
        int64_t time = now()
    ) const noexcept {
        try {
            if (store_ && level >= 1 && level <= 5 && time > 0)
                store_->write(
                    key(downlink),
                    json{{"level", level}, {"time", time}}.dump()
                );
        } catch (...) {
        } // Cache failures must never fail a successfully applied quality change.
    }
};
} // namespace xmax::detail
