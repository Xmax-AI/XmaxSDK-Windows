//
//  UnavailableRtcPlatform.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/RTC/RtcPlatform.hpp"

namespace xmax::detail {

/**
 * 检查当前构建是否包含原生 RTC 后端。
 */
bool rtc_available() noexcept {
    return false;
}

/**
 * 创建原生 RTC 适配器，不支持的平台返回明确的能力错误。
 *
 * @return 可共享的原生 RTC 适配器；平台不支持时抛出能力错误。
 */
std::shared_ptr<Rtc> make_rtc() {
    throw Error(
        XMAX_UNSUPPORTED, "This SDK was built without Agora. Reconfigure with XMAX_WITH_AGORA=ON and "
                          "XMAX_AGORA_ROOT pointing to the native SDK."
    );
}
} // namespace xmax::detail
