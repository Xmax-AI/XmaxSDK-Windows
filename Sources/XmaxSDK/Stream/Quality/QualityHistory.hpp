#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/Storage/PreferenceStore.hpp"
namespace xmax::detail {
class QualityHistory {
    std::string model_;
    std::shared_ptr<PreferenceStore> store_;
    std::string key(bool downlink) const {
        return "quality-agora-" + model_ + (downlink ? "-downlink" : "-uplink");
    }

  public:
    QualityHistory(std::string model, std::shared_ptr<PreferenceStore> store)
        : model_(std::move(model)), store_(std::move(store)) {}
    static int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    int read(bool downlink, int64_t time = now()) const noexcept {
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
    void write(int level, bool downlink, int64_t time = now()) const noexcept {
        try {
            if (store_ && level >= 1 && level <= 5 && time > 0)
                store_->write(key(downlink), json{{"level", level}, {"time", time}}.dump());
        } catch (...) {
        } // Cache failures must never fail a successfully applied quality change.
    }
};
} // namespace xmax::detail
