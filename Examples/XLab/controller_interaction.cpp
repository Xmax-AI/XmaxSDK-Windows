//
//  controller_interaction.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "controller.hpp"

bool XLabController::submitTrajectory(qreal x, qreal y, qreal width, qreal height) {
    if (!handles_ || !remoteReady || !result_width_ || !result_height_)
        return false;
    const xmax_point point{x, y};
    const xmax_interaction_frame sample{
        sizeof(sample), &point, 1, width, height, result_width_, result_height_, XMAX_CONTENT_FILL
    };
    const auto stream = std::atomic_load(&handles_->remote_stream);
    if (!stream) return false;
    try {
        handles_->realtime.submit_stream_interaction(*stream, sample, result_mirrored_);
        return true;
    } catch (const xmax::error&) {
        return false;
    }
}
