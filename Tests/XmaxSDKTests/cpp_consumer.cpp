//
//  cpp_consumer.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include <iostream>
#include <type_traits>
#include <vector>
#include <xmax/xmax.hpp>
static_assert(!std::is_copy_constructible_v<xmax::frame> &&
              std::is_nothrow_move_constructible_v<xmax::frame>);
static_assert(!std::is_copy_constructible_v<xmax::storage> &&
              std::is_nothrow_move_assignable_v<xmax::storage>);
int main() {
    if (xmax::logger::configuration().first != XMAX_LOG_NONE)
        return 20;
    xmax::logger::configure(XMAX_LOG_BUSINESS, XMAX_LOG_ENGLISH);
    xmax::client client("");
    if (!(client.capabilities().flags & XMAX_CAP_STORAGE))
        return 3;
    auto files = xmax::client("offline-test").create_storage();
    auto moved_files = std::move(files);
    if (files || moved_files.progress().state != XMAX_TRANSFER_IDLE)
        return 4;
    moved_files.cancel();
    auto session = client.create_realtime();
    session.prepare({1920, 1024, 90});
    xmax_log_record log{};
    if (!xmax::logger::poll(log) || log.sequence == 0)
        return 21;
    xmax::logger::configure(XMAX_LOG_NONE);
    if (xmax::logger::poll(log))
        return 22;
    const auto binding = session.local_stream();
    const auto track = binding.video_track();
    auto moved = std::move(session);
    if (session || moved.state().connection_state != xmax::realtime_connection_state::ready || moved.format().height != 1024)
        return 5;
    const auto format = track.video_format();
    if (track.id().empty() || !format || format->fps != 90 || track.device() || track.position())
        return 28;
    moved.volume(.4f);
    moved.video_mirror(true, true);
    if (!moved.video_mirror().local_preview || moved.video_mirror().published_video)
        return 23;
    moved.encoding();
    moved.adaptive_quality(false);
    if (moved.volume() != .4f || moved.quality().adaptive_enabled || moved.acquire_frame())
        return 6;
    std::vector<uint8_t> pixels(size_t(1920) * 1024 * 4, 37);
    xmax_video_frame input{};
    input.struct_size = sizeof(input);
    input.pixel_format = XMAX_BGRA;
    input.width = 1920;
    input.height = 1024;
    input.timestamp_us = 1;
    input.planes[0] = pixels.data();
    input.strides[0] = 1920 * 4;
    input.plane_sizes[0] = pixels.size();
    moved.push(input);
    try {
        moved.start_generation(xmax::realtime_context{"requires an existing connection", std::nullopt});
        return 24;
    } catch (const xmax::error& e) {
        if (e.code() != XMAX_INVALID_STATE || moved.state().connection_state != xmax::realtime_connection_state::ready)
            return 25;
    }
    auto other = client.create_realtime();
    try {
        other.connect(binding);
        return 26;
    } catch (const xmax::error& e) {
        if (e.code() != XMAX_INVALID_ARGUMENT)
            return 27;
    }
    auto local = moved.acquire_stream_frame(binding);
    auto kept = std::move(local);
    if (local || !kept || !kept.mirrored() || kept.view().planes[0][0] != 37)
        return 7;
    moved.disconnect();
    moved.close();
    if (kept.view().planes[0][0] != 37 || moved.acquire_local_frame() || track.video_format())
        return 8;
    try {
        moved.start_generation(binding, xmax::realtime_context{"test", std::nullopt});
        return 1;
    } catch (const xmax::error& e) {
        if (e.code() != XMAX_INVALID_STATE)
            return 2;
    }
    std::cout << "C++ wrapper consumer passed\n";
}
