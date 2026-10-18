#pragma once
#include "Foundation/Json.hpp"
#include "Foundation/RTC/RtcPlatform.hpp"
namespace xmax::detail {
json generation_event(const std::string& event, const Connection&, const std::string& task, xmax_video_format,
                      const std::string& prompt, const std::string& reference, bool face);
json room_event(const std::string& event, const Connection&, const std::string& task = {});
} // namespace xmax::detail
