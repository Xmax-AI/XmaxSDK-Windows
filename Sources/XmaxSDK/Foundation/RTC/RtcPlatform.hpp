#pragma once
#include "Foundation/Media/VideoFrame.hpp"
namespace xmax::detail {
struct Connection {
    std::string app_id, room, user, token, bot;
};
struct VideoEncodingConfiguration {
    xmax_video_format format;
    int minimum_bitrate = 100, maximum_bitrate = 100;
    int preference = XMAX_ENCODER_MAINTAIN_FRAMERATE;
};
struct RtcCallbacks {
    std::function<void(Frame)> frame;
    std::function<void(xmax_event)> event;
    std::function<void(Error)> failure;
};
class Rtc {
  public:
    virtual ~Rtc() = default;
    virtual void join(const Connection&, VideoEncodingConfiguration, bool microphone, RtcCallbacks,
                      const Cancel&) = 0;
    virtual void send(const std::string&) = 0;
    virtual void push(const Frame&) = 0;
    virtual void begin_generation() = 0;
    virtual void wait_remote(const Cancel&, const std::function<void()>& keepalive) = 0;
    virtual void volume(float) = 0;
    virtual void configure_encoding(VideoEncodingConfiguration) = 0;
    // Must drain callbacks before returning.
    virtual void leave() noexcept = 0;
};
using RtcFactory = std::function<std::shared_ptr<Rtc>()>;
bool rtc_available() noexcept;
std::shared_ptr<Rtc> make_rtc();
} // namespace xmax::detail
