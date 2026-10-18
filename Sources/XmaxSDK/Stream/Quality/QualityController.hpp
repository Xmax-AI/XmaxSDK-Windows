#pragma once
#include "Foundation/Media/VideoFrame.hpp"
#include <optional>
namespace xmax::detail {
xmax_video_format quality_format(xmax_video_format baseline, int level, bool downlink);
// macOS policy: 3 poor samples downgrade, 5 good samples upgrade, cooldown and probe backoff.
class QualityDirection {
  public:
    int level = 2;
    std::optional<int> observe(int quality, double sampled_at, double now);
    void complete(int next, bool success, double now);
    void reset_samples();

  private:
    int good_ = 0, poor_ = 0;
    double last_ = -1, stable_until_ = 0, upgrade_after_ = 0, probe_until_ = 0;
};
class QualityController {
  public:
    QualityDirection up, down;
    bool enabled = true;
    void reset_samples() {
        up.reset_samples();
        down.reset_samples();
    }
};
} // namespace xmax::detail
