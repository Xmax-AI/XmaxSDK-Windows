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
 * Reports whether the current build includes a native RTC backend.
 */
bool rtc_available() noexcept {
    return false;
}

/**
 * Creates a native RTC adapter or reports unsupported capability on other platforms.
 *
 * @return A shared native RTC adapter; unsupported platforms throw a capability error.
 */
std::shared_ptr<Rtc> make_rtc() {
    throw Error(
        XMAX_UNSUPPORTED,
        "This SDK was built without Agora. Reconfigure with XMAX_WITH_AGORA=ON and "
        "XMAX_AGORA_ROOT pointing to the native SDK."
    );
}
} // namespace xmax::detail
