#include <iostream>
#include <type_traits>
#include <vector>
#include <xmax/xmax.hpp>
static_assert(!std::is_copy_constructible_v<xmax::frame> &&
              std::is_nothrow_move_constructible_v<xmax::frame>);
static_assert(!std::is_copy_constructible_v<xmax::storage> &&
              std::is_nothrow_move_assignable_v<xmax::storage>);
int main() {
    xmax::client client("");
    if (!(client.capabilities().flags & XMAX_CAP_STORAGE))
        return 3;
    auto files = client.create_storage();
    auto moved_files = std::move(files);
    if (files || moved_files.progress().state != XMAX_TRANSFER_IDLE)
        return 4;
    moved_files.cancel();
    auto session = client.create_realtime();
    session.prepare({1920, 1024, 30});
    auto moved = std::move(session);
    if (session || moved.state().state != XMAX_READY || moved.format().height != 1024)
        return 5;
    moved.volume(.4f);
    moved.encoding();
    moved.adaptive_quality(false);
    moved.interpolation(false);
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
    auto local = moved.acquire_local_frame();
    auto kept = std::move(local);
    if (local || !kept || kept.view().planes[0][0] != 37)
        return 7;
    moved.disconnect();
    moved.close();
    if (kept.view().planes[0][0] != 37 || moved.acquire_local_frame())
        return 8;
    try {
        moved.start("test");
        return 1;
    } catch (const xmax::error& e) {
        if (e.code() != XMAX_INVALID_STATE)
            return 2;
    }
    std::cout << "C++ wrapper consumer passed\n";
}
