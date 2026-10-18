#pragma once
#include "Media/Camera/CameraController.hpp"
#include "Media/External/ExternalVideoController.hpp"
namespace xmax::detail {
class MediaController {
  public:
    explicit MediaController(CameraFactory factory = make_camera_capture) : camera_(std::move(factory)) {}
    ~MediaController() { close(); }
    ExternalVideoController& external() { return external_; }
    const ExternalVideoController& external() const { return external_; }
    void stop() { external_.stop(); }
    void close() {
        camera_.stop();
        external_.close();
        camera_active_ = false;
        device_.clear();
    }
    void prepare_external(xmax_video_format format) {
        close();
        external_.prepare(format);
    }
    void prepare_camera(const std::string& device, xmax_video_format format,
                        std::function<void(Error)> failure, const Cancel& cancel) {
        close();
        external_.prepare(format);
        camera_active_ = true;
        device_ = device;
        try {
            camera_.start(
                device, format,
                {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)},
                cancel);
        } catch (...) {
            close();
            throw;
        }
    }
    void switch_camera(const std::string& device, std::function<void(Error)> failure, const Cancel& cancel) {
        require(camera_active_, XMAX_INVALID_STATE, "No camera stream is active");
        camera_.stop();
        external_.prepare(external_.format());
        camera_.start(device, external_.format(),
                      {[this](Frame frame) { external_.push_owned(std::move(frame)); }, std::move(failure)},
                      cancel);
        device_ = device;
    }
    bool camera_active() const { return camera_active_.load(); }
    std::vector<CameraDevice> devices() { return camera_.devices(); }

  private:
    ExternalVideoController external_;
    CameraController camera_;
    std::atomic<bool> camera_active_{false};
    std::string device_;
};
} // namespace xmax::detail
