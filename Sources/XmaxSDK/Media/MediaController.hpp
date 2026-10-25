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
#include "Media/External/ExternalVideoController.hpp"

namespace xmax::detail {

/**
 * Manages mutually exclusive SDK camera and external inputs through a shared frame-delivery channel.
 */
class MediaController {

  public:

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

    /**
     * Stops capture and clears input binding and device state.
     */
    void close() {
        camera_.stop();
        external_.close();
        camera_active_ = false;
        device_.clear();
    }

    /**
     * Replaces the media source and binds a supported external format.
     *
     * @param[in] format The input dimensions and frame rate; see xmax_video_format for limits.
     * @param[in] binding The revision rejecting frames from replaced external sources.
     */
    void prepare_external(
        xmax_video_format format,
        uint64_t binding = 0
    ) {
        close();
        external_.prepare(
            format,
            binding
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
        require(
            camera_active_,
            XMAX_INVALID_STATE,
            "No camera stream is active"
        );
        camera_.stop();
        external_.prepare(external_.format());
        camera_.start(
            device,
            external_.format(),
            {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)},
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
        require(
            camera_active_,
            XMAX_INVALID_STATE,
            "No camera stream is active"
        );

        const auto previous = external_.format();
        camera_.stop();
        external_.prepare(format);

        try {
            camera_.start(
                device_,
                format,
                {[this](Frame frame) { external_.push_owned(std::move(frame)); }, failure},
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
                        {[this](Frame frame) { external_.push_owned(std::move(frame)); }, failure},
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

  private:

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
