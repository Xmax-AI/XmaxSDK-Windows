//
//  RoomEvent.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Room/RoomEvent.hpp"
#include "Foundation/Runtime/RuntimeInfo.hpp"
#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <algorithm>

namespace xmax::detail {

/**
 * Builds signaling with room, task, video format, and generation conditions.
 *
 * @param[in] event The generation signaling event name.
 * @param[in] c The current RTC room connection information.
 * @param[in] task The current generation task identifier; empty where control signaling permits omission.
 * @param[in] format The model-resolved generation canvas dimensions and frame rate.
 * @param[in] prompt The generation prompt, encoded as UTF-8.
 * @param[in] reference A reference image path or service-supported URL; empty omits the reference.
 * @param[in] face Whether to enable face restoration for generation.
 */
json generation_event(
    const std::string& event,
    const Connection& c,
    const std::string& task,
    xmax_video_format format,
    const std::string& prompt,
    const std::string& reference,
    bool face
) {
    validate_format(format);

    json params{
        {"model", "default"},
        {"size", json::array({format.width, format.height})},
        {"target_size", json::array({format.width / 4 * 4, format.height / 4 * 4})},
        {"prompt", prompt},
        {"face_replace", face},
        {"padding", "contain"}
    };

    if (!reference.empty())
        params["ref_image_path"] = reference;

    if (event == "start") {
        params["mirror"] = false;
        params["min_bitrate"] = 100;
        params["max_bitrate"] = std::min(10000, bitrate({format.width, format.height, 24}).second);
    }

    return {
        {"event", event},
        {"params", params},
        {"user_id", c.user},
        {"uid", task},
        {"session_uid", task},
        {"runtime", runtime_info()}
    };
}

/**
 * Builds a room control message with an optional task identifier.
 *
 * @param[in] event The room control event name.
 * @param[in] connection The current RTC room connection information.
 * @param[in] task The current generation task identifier; empty where control signaling permits omission.
 */
json room_event(
    const std::string& event,
    const Connection& connection,
    const std::string& task
) {
    json value{{"event", event}, {"user_id", connection.user}, {"runtime", runtime_info()}};
    if (!task.empty()) {
        value["uid"] = task;
        value["session_uid"] = task;
    }
    return value;
}
} // namespace xmax::detail
