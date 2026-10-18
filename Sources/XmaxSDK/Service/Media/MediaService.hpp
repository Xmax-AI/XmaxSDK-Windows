#pragma once
#include "Foundation/Media/VideoFrame.hpp"
namespace xmax::detail {
class MediaService {
  public:
    static xmax_video_format input(xmax_video_format format) {
        validate_format(format);
        require((format.width == 1920 && format.height == 1024) ||
                    (format.width == 1024 && format.height == 1920),
                XMAX_INVALID_ARGUMENT, "The model accepts 1920x1024 or 1024x1920 camera/external input");
        return format;
    }
};
} // namespace xmax::detail
