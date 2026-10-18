#pragma once
#include "Core/Realtime/RealtimeCoordinator.hpp"
#include "Media/MediaController.hpp"
#include "Render/RenderController.hpp"
#include "Service/Realtime/RealtimeSessionService.hpp"
#include "Stream/StreamController.hpp"
#include <optional>
namespace xmax::detail {
// Composes services and controllers; protocol, input and rendering are delegated to their owners.
class XmaxRealtimeManager {
  public:
    explicit XmaxRealtimeManager(std::shared_ptr<ApiService>, RtcFactory = make_rtc,
                                 std::chrono::milliseconds heartbeat = std::chrono::seconds(10),
                                 CameraFactory = make_camera_capture);
    ~XmaxRealtimeManager();
    void prepare(xmax_video_format);
    void start(std::string prompt, std::string reference, bool face, bool microphone);
    void update(std::string prompt, std::string reference, bool face);
    void disconnect(bool close);
    void push(const xmax_video_frame& frame);
    void prepare_camera(std::string device, xmax_video_format, bool microphone);
    void switch_camera(std::string device);
    void stop_media();
    void connect(bool microphone);
    void generate(const xmax_generation_context*);
    xmax_event state_snapshot();
    xmax_video_format format();
    float volume();
    void interpolation(bool enabled);
    void encoding(xmax_encoding_config value);
    void adaptive(bool enabled);
    xmax_quality_status quality();
    bool requesting() const;
    void volume(float);
    bool poll(xmax_event& event) { return coordinator_.poll(event); }
    std::shared_ptr<Frame> acquire_frame() { return render_.acquire_frame(); }
    std::shared_ptr<Frame> acquire_local_frame() { return render_.acquire_local_frame(); }

  private:
    std::shared_ptr<ApiService> api_;
    RealtimeSessionService service_;
    StreamController stream_;
    MediaController media_;
    RenderController render_;
    std::atomic<uint64_t> input_epoch_{0};
    std::atomic<uint64_t> media_epoch_{0};
    bool camera_microphone_ = false;
    struct Context {
        std::string prompt, reference;
        bool face = false;
    };
    std::optional<Context> context_;
    xmax_event launch_timing_{};
    Clock::time_point launch_started_;
    std::chrono::milliseconds heartbeat_interval_;
    RealtimeCoordinator coordinator_;
    void change_state(xmax_state);
    bool cleanup(bool close);
    void heartbeat();
    void keepalive(const Cancel&);
    void fail(Error);
    void connect_impl(const Cancel&, uint64_t epoch, bool microphone);
    void generate_impl(const Context&, const Cancel&);
    void bind_local();
    std::function<void(Error)> media_failure(uint64_t token);
};
} // namespace xmax::detail
