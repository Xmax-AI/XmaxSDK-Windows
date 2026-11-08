//
//  XmaxRealtimeManager.Camera.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"
#include "Service/Media/MediaService.hpp"
#include <algorithm>
#include <thread>

namespace xmax::detail {

/**
 * Serializes camera preparation with realtime lifecycle operations.
 */
using Lifecycle = XmaxRealtimeLifecycleManager;

/**
 * Identifies the media operation being prepared.
 */
using OperationKind = Lifecycle::OperationKind;

/**
 * Defines cleanup required if camera preparation fails.
 */
using Scope = Lifecycle::TerminationScope;

/**
 * Starts a camera, waits for its first frame, and enters ready state with frames in the configured format.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
 * @param[in] microphone Whether to start microphone capture when joining the RTC room.
 * @param[in] mirror Whether to mirror local preview and camera publication.
 * @param[in] microphone_device Recording endpoint identifier; empty selects the system default.
 */
void XmaxRealtimeManager::create_local_camera_stream(
    std::string device,
    xmax_video_format format,
    bool microphone,
    bool mirror,
    std::string microphone_device,
    bool wait_for_preview,
    std::optional<xmax_encoding_config> encoding
) {
    lifecycle_.run(
        OperationKind::Media,
        [
            this,
            device = std::move(device),
            format,
            microphone,
            mirror,
            microphone_device = std::move(microphone_device),
            wait_for_preview,
            encoding
        ](const Lifecycle::Token& token) {
            require(
                lifecycle_.state() == XMAX_IDLE || (!wait_for_preview && lifecycle_.state() == XMAX_READY),
                XMAX_INVALID_STATE,
                "Disconnect before replacing local media"
            );
            const auto resolved = MediaService::camera_input(format, model_info());
            if (encoding)
                EncodingController::validate(*encoding);
            if (microphone)
                media_.microphone().validate(microphone_device);
            token.failure_scope(Scope::All);
            errors_.reset();
            change_state(XMAX_PREPARING);
            stream_.mirror(mirror);
            render_.mirror(
                mirror,
                mirror_.remote_view != 0
            );
            mirror_.local_preview = mirror_.published_video = mirror ? 1 : 0;
            bind_local();
            media_.prepare_camera(
                device,
                resolved,
                media_failure(media_epoch_),
                token.cancellation()
            );
            token.ensure_current();
            if (encoding)
                stream_.encoding(*encoding);
            media_.microphone().configure(microphone, std::move(microphone_device));
            stream_.volume(0);
            camera_preview_pending_ = wait_for_preview;
            if (!wait_for_preview)
                complete_state(XMAX_READY, token);
        }
    );
}

bool XmaxRealtimeManager::can_connect() const {
    const auto state = lifecycle_.state();
    return state == XMAX_READY || (state == XMAX_PREPARING && camera_preview_pending_ && media_.camera_active());
}

void XmaxRealtimeManager::complete_camera_preview(
    uint64_t revision
) {
    if (revision != local_stream_revision() || !camera_preview_pending_)
        return;
    lifecycle_.sync([this, revision] {
        if (revision == local_stream_revision() && camera_preview_pending_ && media_.camera_active() &&
            lifecycle_.state() == XMAX_PREPARING) {
            camera_preview_pending_ = false;
            change_state(XMAX_READY);
        }
    });
}

std::shared_ptr<Frame> XmaxRealtimeManager::acquire_local_frame(
    uint64_t revision
) {
    std::lock_guard<std::mutex> lock(media_binding_mutex_);
    if (revision && revision != local_stream_revision())
        return {};
    return render_.acquire_local_frame();
}

/**
 * Switches the active camera and resumes generation with retained conditions.
 *
 * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
 */
void XmaxRealtimeManager::switch_camera(
    std::string device
) {
    lifecycle_.run(
        OperationKind::CameraSwitch,
        [this, device = std::move(device)](const Lifecycle::Token& token) {
            const auto state = lifecycle_.state();
            const bool connected = state == XMAX_CONNECTED || state == XMAX_GENERATING;
            require(
                media_.camera_active() && (state == XMAX_PREPARING || state == XMAX_READY || connected),
                XMAX_INVALID_STATE,
                "No switchable camera stream is active"
            );

            const bool generating = state == XMAX_GENERATING;
            const auto cancel = token.cancellation();
            const auto epoch = lifecycle_.epoch();
            if (generating) {
                token.failure_scope(Scope::Connection);
                stream_.stop_generation(cancel);
                change_state(XMAX_CONNECTED);
            }
            media_.stop();
            render_.unbind(epoch);
            bind_local(true);

            const auto resume_publication = [this, epoch] {
                auto connection_cancel = lifecycle_.cancellation(epoch);
                media_.external().start([this, connection_cancel](const Frame& frame) {
                    if (!connection_cancel())
                        stream_.push(frame);
                });
                render_.bind(epoch);
            };

            try {
                media_.switch_camera(
                    device,
                    media_failure(media_epoch_),
                    cancel
                );
                token.ensure_current();
            } catch (...) {
                if (!media_.camera_active())
                    token.failure_scope(Scope::All);
                else if (state == XMAX_CONNECTED && !cancel())
                    resume_publication();
                throw;
            }

            if (connected)
                resume_publication();
            if (generating) {
                const auto settled = Clock::now() + std::chrono::milliseconds(500);
                while (Clock::now() < settled) {
                    token.ensure_current();
                    std::this_thread::sleep_until(std::min(
                        settled,
                        Clock::now() + std::chrono::milliseconds(10)
                    ));
                }
                token.ensure_current();
                timing_.begin();
                generate_impl(
                    *generation_.context,
                    cancel
                );
            }

            complete_state(
                state,
                token,
                [this, generating] {
                    if (generating) {
                        timing_.finish();
                        emit_timing(timing_.snapshot());
                    }
                }
            );
        }
    );
}

} // namespace xmax::detail
