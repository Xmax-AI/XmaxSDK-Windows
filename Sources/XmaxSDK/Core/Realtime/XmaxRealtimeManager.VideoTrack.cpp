//
//  XmaxRealtimeManager.VideoTrack.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Core/Realtime/XmaxRealtimeManager.hpp"

namespace xmax::detail {

bool XmaxRealtimeManager::read_video_track(
    uint64_t revision,
    bool local,
    xmax_video_format& format,
    xmax_encoding_config* encoding,
    std::optional<CameraDevice>* camera
) {
    bool found = false;
    lifecycle_.sync([&] {
        if (local) {
            if (revision != local_stream_revision() || !media_.external().bound())
                return;
            format = media_.external().format();
            if (camera)
                *camera = media_.current_camera();
        } else {
            if (revision != lifecycle_.epoch() + 1)
                return;
            const auto quality = stream_.quality();
            if (!quality.downlink_level)
                return;
            format = quality.downlink;
            format.fps = media_.external().format().fps;
        }
        if (encoding)
            *encoding = stream_.encoding();
        found = true;
    });
    return found;
}

} // namespace xmax::detail
