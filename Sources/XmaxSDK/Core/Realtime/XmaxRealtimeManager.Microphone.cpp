//
//  XmaxRealtimeManager.Microphone.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"

namespace xmax::detail {

void XmaxRealtimeManager::switch_microphone(
    std::string device
) {
    lifecycle_.run(
        XmaxRealtimeLifecycleManager::OperationKind::Configuration,
        [this, device = std::move(device)](const XmaxRealtimeLifecycleManager::Token& token) {
            const auto state = lifecycle_.state();
            require(
                media_.camera_active() && (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
                XMAX_INVALID_STATE,
                "Microphone changes require a ready camera stream"
            );
            bool sampling;
            {
                std::lock_guard<std::mutex> lock(microphone_level_mutex_);
                sampling = microphone_sampling_;
            }
            if ((camera_microphone_ || sampling) && microphone_validator_)
                microphone_validator_(device);
            if (sampling)
                set_microphone_sampling(false);
            try {
                stream_.microphone_device(device);
                camera_microphone_device_ = device;
            } catch (...) {
                if (sampling)
                    set_microphone_sampling(true);
                throw;
            }
            if (sampling)
                set_microphone_sampling(true);
            token.ensure_current();
        }
    );
}

void XmaxRealtimeManager::set_microphone_enabled(
    bool enabled
) {
    lifecycle_.run(
        XmaxRealtimeLifecycleManager::OperationKind::Configuration,
        [this, enabled](const XmaxRealtimeLifecycleManager::Token& token) {
            const auto state = lifecycle_.state();
            require(
                media_.camera_active() && (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
                XMAX_INVALID_STATE,
                "Microphone changes require a ready camera stream"
            );
            if (enabled && microphone_validator_)
                microphone_validator_(camera_microphone_device_);
            if (!enabled) {
                camera_microphone_ = false;
                set_microphone_sampling(false);
            }

            if (state == XMAX_CONNECTED || state == XMAX_GENERATING) {
                stream_.microphone_enabled(enabled);
                if (enabled)
                    set_microphone_sampling(true);
            }
            camera_microphone_ = enabled;
            token.ensure_current();
        }
    );
}

bool XmaxRealtimeManager::microphone_enabled() {
    bool enabled = false;
    lifecycle_.sync([this, &enabled] { enabled = media_.camera_active() && camera_microphone_; });
    return enabled;
}

void XmaxRealtimeManager::emit_microphone_level(int sample) {
    xmax_event level{};
    level.type = XMAX_EVENT_MICROPHONE_LEVEL;
    level.code = sample;
    lifecycle_.emit(level);
}

void XmaxRealtimeManager::set_microphone_sampling(bool enabled) {
    std::lock_guard<std::mutex> lock(microphone_level_mutex_);
    if (microphone_sampling_ && !enabled)
        emit_microphone_level(-1);
    microphone_sampling_ = enabled;
}

} // namespace xmax::detail
