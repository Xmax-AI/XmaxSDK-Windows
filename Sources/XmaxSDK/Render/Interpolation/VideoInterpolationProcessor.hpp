//
//  VideoInterpolationProcessor.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <optional>

namespace xmax::detail {

struct InterpolationOptions {
    std::string model_directory;
    int device_index = 0;
    uint32_t cpu_threads = 4;
    uint32_t buffer_ms = 125;
};

inline void validate_interpolation(const InterpolationOptions& options) {
    require(options.device_index >= -1 && options.cpu_threads >= 1 && options.cpu_threads <= 32 &&
                options.buffer_ms >= 50 && options.buffer_ms <= 500,
            XMAX_INVALID_ARGUMENT, "Invalid interpolation device, thread count, or buffer duration");
}

struct InterpolationTransferStats {
    uint64_t prepared_frames = 0;
    uint64_t uploaded_frames = 0;
    uint64_t downloaded_frames = 0;
};

/** Inputs remain immutable while retained. All calls run on one inference thread. */
class VideoInterpolationProcessor {
  public:
    virtual ~VideoInterpolationProcessor() = default;
    virtual std::shared_ptr<Frame> midpoint(std::shared_ptr<const Frame> previous,
                                          std::shared_ptr<const Frame> next) = 0;
    virtual void reset() {}
    virtual InterpolationTransferStats transfers() const { return {}; }
};

using InterpolationProcessorFactory =
    std::function<std::unique_ptr<VideoInterpolationProcessor>(const InterpolationOptions&)>;
std::unique_ptr<VideoInterpolationProcessor> make_video_interpolation_processor(const InterpolationOptions& options);

} // namespace xmax::detail
