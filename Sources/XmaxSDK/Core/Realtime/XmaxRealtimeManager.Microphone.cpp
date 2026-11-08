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
        [this, device = std::move(device)](const XmaxRealtimeLifecycleManager::Token& token) mutable {
            const auto state = lifecycle_.state();
            require(
                media_.camera_active() && (state == XMAX_READY || state == XMAX_CONNECTED || state == XMAX_GENERATING),
                XMAX_INVALID_STATE,
                "Microphone changes require a ready camera stream"
            );
            media_.microphone().select(std::move(device));
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
            auto& microphone = media_.microphone();
            const bool connected = state == XMAX_CONNECTED || state == XMAX_GENERATING;
            if (!enabled) {
                microphone.stop();
                microphone.enabled(false);
                if (connected)
                    stream_.audio_publication(false);
            } else {
                microphone.validate();
                if (connected && !microphone.running()) {
                    prepare_microphone(lifecycle_.epoch());
                    try {
                        token.ensure_current();
                        stream_.audio_publication(true);
                    } catch (...) {
                        microphone.stop();
                        throw;
                    }
                    microphone.activate();
                }
                microphone.enabled(true);
            }
            token.ensure_current();
        }
    );
}

bool XmaxRealtimeManager::microphone_enabled() {
    bool enabled = false;
    lifecycle_.sync([this, &enabled] { enabled = media_.camera_active() && media_.microphone().enabled(); });
    return enabled;
}

void XmaxRealtimeManager::prepare_microphone(uint64_t epoch) {
    const auto cancelled = lifecycle_.cancellation(epoch);
    MicrophoneCapture::Level level;
    if (media_.camera_active()) {
        level = [this, cancelled](int sample) {
            if (sample < 0 || !cancelled())
                emit_microphone_level(sample);
        };
    }
    media_.microphone().prepare(
        [this, cancelled](const ExternalAudioPacket& packet) {
            if (!cancelled())
                stream_.push_audio(packet);
        },
        std::move(level)
    );
}

void XmaxRealtimeManager::emit_microphone_level(int sample) {
    xmax_event level{};
    level.type = XMAX_EVENT_MICROPHONE_LEVEL;
    level.code = sample;
    lifecycle_.emit(level);
}

} // namespace xmax::detail
