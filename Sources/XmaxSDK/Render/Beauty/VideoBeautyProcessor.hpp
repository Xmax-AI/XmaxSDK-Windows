//
//  VideoBeautyProcessor.hpp
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
#include <cmath>

namespace xmax::detail {
using BeautyOptions = xmax_realtime_video_beauty_options;
using BeautyErrorListener = std::function<void(Error)>;

inline void validate_beauty(const BeautyOptions& options) {
    for (float value : {options.smoothness, options.lightening, options.sharpness, options.redness})
        require(std::isfinite(value) && value >= 0 && value <= 1, XMAX_INVALID_ARGUMENT,
                "Beauty strengths must be finite numbers from 0 to 1");
}

/** A private offline processor; never joins RTC or receives account credentials. */
class VideoBeautyProcessor {
  public:
    virtual ~VideoBeautyProcessor() = default;
    virtual void update(BeautyOptions options) = 0;
    virtual std::shared_ptr<Frame> process(const Frame& frame) = 0;
};
using BeautyProcessorFactory = std::function<std::unique_ptr<VideoBeautyProcessor>(BeautyOptions)>;
std::unique_ptr<VideoBeautyProcessor> make_video_beauty_processor(BeautyOptions options);
}
