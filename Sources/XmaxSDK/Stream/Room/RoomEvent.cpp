#include "Stream/Room/RoomEvent.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <algorithm>
namespace xmax::detail {
json generation_event(const std::string& event, const Connection& c, const std::string& task,
                      xmax_video_format format, const std::string& prompt, const std::string& reference,
                      bool face) {
    validate_format(format);
    // Model canvas is independent from the externally supplied media aspect ratio.
    const bool landscape = format.width >= format.height;
    json params{{"model", "default"},
                {"size", landscape ? json::array({1920, 1024}) : json::array({1024, 1920})},
                {"target_size", json::array({format.width / 4 * 4, format.height / 4 * 4})},
                {"prompt", prompt},
                {"face_replace", face},
                {"padding", "contain"}};
    if (!reference.empty())
        params["ref_image_path"] = reference;
    if (event == "start") {
        params["mirror"] = false;
        params["min_bitrate"] = 100;
        params["max_bitrate"] = std::min(10000, bitrate({format.width, format.height, 24}).second);
    }
    return {{"event", event}, {"params", params},    {"user_id", c.user},
            {"uid", task},    {"session_uid", task}, {"runtime", runtime_info()}};
}
json room_event(const std::string& event, const Connection& connection, const std::string& task) {
    json value{{"event", event}, {"user_id", connection.user}, {"runtime", runtime_info()}};
    if (!task.empty()) {
        value["uid"] = task;
        value["session_uid"] = task;
    }
    return value;
}
} // namespace xmax::detail
