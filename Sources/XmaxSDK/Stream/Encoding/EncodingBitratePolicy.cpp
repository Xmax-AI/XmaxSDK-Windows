//
//  EncodingBitratePolicy.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Stream/Encoding/EncodingBitratePolicy.hpp"
#include <algorithm>

namespace xmax::detail {

/**
 * 根据已验证的视频规格计算策略最小码率与估计最大码率。
 *
 * @param[in] f 视频尺寸和帧率，宽高须为偶数、总像素不超过 2100000、帧率范围为 [1, 60]。
 * @return 以 kbps 为单位的策略最小码率和估计最大码率。
 */
std::pair<int, int> bitrate(
    xmax_video_format f
) {
    validate_format(f);

    using P = std::pair<double, double>;
    const std::vector<P> p15{{14400, 50},   {19200, 65},   {32400, 100},  {43200, 120},   {57600, 140},
                             {76800, 200},  {101760, 220}, {129600, 260}, {172800, 320},  {230400, 400},
                             {307200, 500}, {407040, 610}, {691200, 910}, {921600, 1130}, {2073600, 2080}};
    const std::vector<P> p30{{129600, 400}, {172800, 490},  {230400, 600},  {307200, 750},
                             {407040, 930}, {691200, 1380}, {921600, 1710}, {2073600, 3150}};
    auto interpolate = [](double v, const std::vector<P>& p) {
        if (v <= p.front().first)
            return p.front().second * v / p.front().first;
        for (size_t i = 1; i < p.size(); ++i)
            if (v <= p[i].first) {
                const auto a = p[i - 1], b = p[i];
                return a.second + (b.second - a.second) * (v - a.first) / (b.first - a.first);
            }
        return p.back().second * v / p.back().first;
    };
    const double pixels = double(f.width) * f.height, b15 = interpolate(pixels, p15);
    const double b30 = pixels < 129600 ? b15 * 400 / interpolate(129600, p15) : interpolate(pixels, p30);
    const double maximum =
        interpolate(f.fps, {{10, b15 * 0.8 * 2}, {15, b15 * 2}, {30, b30 * 2}, {60, b30 * 6500 / 3150}}) *
        0.6;

    return {100, std::max(100, int(maximum))};
}
} // namespace xmax::detail
