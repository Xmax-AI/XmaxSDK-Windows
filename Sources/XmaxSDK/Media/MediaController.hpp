//
//  MediaController.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Media/Camera/CameraController.hpp"
#include "Media/Interaction/InteractionController.hpp"
#include "Media/External/ExternalVideoController.hpp"
#include <algorithm>

namespace xmax::detail {

/**
 * Manages mutually exclusive SDK camera and external inputs through a shared frame-delivery channel.
 */
class MediaController {

  public:

    /**
     * The local interaction samples waiting for generation-task delivery.
     */
    InteractionController interaction;

    /**
     * Combines camera capture and external frame delivery using the supplied camera factory.
     */
    explicit MediaController(
        CameraFactory factory = make_camera_capture
    )
        : camera_(std::move(factory)) {
    }

    /**
     * Stops camera callbacks, clears external input binding, and releases local media resources.
     */
    ~MediaController() {
        close();
    }

    /**
     * Returns a borrowed reference to the shared frame-delivery controller.
     *
     * @return The internal frame-delivery controller; invalid after its containing object is destroyed.
     */
    ExternalVideoController& external() {
        return external_;
    }

    /**
     * Returns a borrowed reference to the shared frame-delivery controller.
     *
     * @return The internal frame-delivery controller; invalid after its containing object is destroyed.
     */
    const ExternalVideoController& external() const {
        return external_;
    }

    /**
     * Stops publication delivery while retaining bound media and local preview.
     */
    void stop() {
        external_.stop();
    }

    /** Starts releasing SDK-owned capture while leaving binding cleanup to the lifecycle worker. */
    std::future<void> stop_local_capture() {
        return camera_.stop_async();
    }

    /**
     * Stops capture and clears input binding and device state.
     */
    void close() {
        camera_.stop();
        external_.close();
        interaction.clear();
        camera_active_ = false;
        device_.clear();
    }

    /**
     * Replaces the media source and binds a supported external format.
     *
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] binding The revision rejecting frames from replaced external sources.
     * @param[in] has_audio Whether this source supplies PCM instead of microphone input.
     * @param[in] processing_format The normalized output format, or omitted for exact source dimensions.
     */
    void prepare_external(
        xmax_video_format format,
        uint64_t binding = 0,
        bool has_audio = false,
        std::optional<xmax_video_format> processing_format = {}
    ) {
        close();
        external_.prepare(
            format,
            binding,
            has_audio,
            processing_format
        );
    }

    /**
     * Replaces the media source, starts SDK camera capture, and waits for the first frame.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] failure The asynchronous failure callback; delivery must finish before stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void prepare_camera(
        const std::string& device,
        xmax_video_format format,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        close();
        external_.prepare(format);
        camera_active_ = true;
        device_ = device;

        try {
            camera_.start(
                device,
                format,
                {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)},
                cancel
            );
        } catch (...) {
            close();
            throw;
        }
    }

    /**
     * Switches camera capture using the current input format.
     *
     * @param[in] device The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] failure The asynchronous failure callback; delivery must finish before stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void switch_camera(
        const std::string& device,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        reconfigure_camera(
            device,
            external_.format(),
            std::move(failure),
            cancel
        );
        device_ = device;
    }

    /**
     * Updates the camera target format while retaining device selection and preview callbacks.
     *
     * @param[in] format The new encoding target dimensions and frame rate.
     * @param[in] failure The current media binding's failure callback.
     * @param[in] cancel The cooperative cancellation predicate.
     */
    void update_camera_format(
        xmax_video_format format,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        reconfigure_camera(
            device_,
            format,
            std::move(failure),
            cancel
        );
    }

    /**
     * Reports whether SDK camera input is bound.
     */
    bool camera_active() const {
        return camera_active_.load();
    }

    /**
     * Enumerates camera devices without starting capture.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     */
    std::vector<CameraDevice> devices() {
        return camera_.devices();
    }

    /** Reads the selected camera from the current device enumeration. */
    std::optional<CameraDevice> current_camera() {
        if (!camera_active())
            return {};
        const auto cameras = devices();
        if (cameras.empty())
            return {};
        const auto selected = std::find_if(cameras.begin(), cameras.end(), [this](const auto& camera) {
            return device_.empty() ? camera.is_default : camera.id == device_;
        });
        if (selected != cameras.end())
            return *selected;
        if (device_.empty())
            return cameras.front();
        return {};
    }

  private:

    /**
     * Restarts capture on the requested device and restores the previous capture on failure.
     *
     * @param[in] device The requested device identifier; empty selects the default camera.
     * @param[in] format The new encoding target.
     * @param[in] failure The current media binding's failure callback.
     * @param[in] cancel The cooperative cancellation predicate.
     */
    void reconfigure_camera(
        const std::string& device,
        xmax_video_format format,
        std::function<void(Error)> failure,
        const Cancel& cancel
    ) {
        require(
            camera_active_,
            XMAX_INVALID_STATE,
            "No camera stream is active"
        );

        const auto previous = external_.format();
        camera_.stop();
        const CaptureCallbacks callbacks{
            [this](Frame frame) { external_.push_owned(std::move(frame)); },
            std::move(failure)
        };
        try {
            external_.prepare(format);
            camera_.start(
                device,
                format,
                callbacks,
                cancel
            );
        } catch (...) {
            const auto original = std::current_exception();
            if (cancel()) {
                close();
            } else {
                try {
                    external_.prepare(previous);
                    camera_.start(
                        device_,
                        previous,
                        callbacks,
                        cancel
                    );
                } catch (...) {
                    close();
                }
            }
            std::rethrow_exception(original);
        }
    }

    /**
     * The shared controller for input publication and local preview delivery.
     */
    ExternalVideoController external_;

    /**
     * The controller for native SDK camera capture and frame delivery.
     */
    CameraController camera_;

    /**
     * The atomic flag indicating an SDK camera binding.
     */
    std::atomic<bool> camera_active_{false};

    /**
     * The current camera's device identifier.
     */
    std::string device_;
};
} // namespace xmax::detail
