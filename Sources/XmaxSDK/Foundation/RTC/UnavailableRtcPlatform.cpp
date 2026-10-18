#include "Foundation/RTC/RtcPlatform.hpp"
namespace xmax::detail {
bool rtc_available() noexcept {
    return false;
}
std::shared_ptr<Rtc> make_rtc() {
    throw Error(XMAX_UNSUPPORTED, "This SDK was built without Agora. Reconfigure with XMAX_WITH_AGORA=ON and "
                                  "XMAX_AGORA_ROOT pointing to the native SDK.");
}
} // namespace xmax::detail
