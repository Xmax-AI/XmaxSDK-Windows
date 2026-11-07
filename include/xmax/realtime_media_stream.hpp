//
//  realtime_media_stream.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once

#include "realtime_video_track.hpp"

namespace xmax {

class realtime;

/** Identifies a local or result stream issued by a realtime controller. */
class realtime_media_stream {
  public:
    /** Whether this stream represents local input. */
    bool is_local() const noexcept { return track_.local_; }

    /** Returns the track with live format and device metadata. */
    realtime_video_track video_track() const { return track_; }

  private:
    friend class realtime;
    realtime_media_stream(const std::shared_ptr<realtime_video_track::binding>& owner, uint64_t revision, bool local)
        : track_(owner, revision, local) {}

    realtime_video_track track_;
};

} // namespace xmax
