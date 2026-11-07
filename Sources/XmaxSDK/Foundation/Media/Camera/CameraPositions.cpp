//
//  CameraPositions.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/Camera/CameraPositions.hpp"

#if defined(_MSC_VER) && __has_include(<winrt/Windows.Devices.Enumeration.h>)
#include <future>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>

namespace xmax::detail {
namespace {

/**
 * Initializes and balances WinRT on a camera metadata worker.
 */
struct DeviceApartment {

    /**
     * Initializes a multithreaded apartment for blocking device enumeration.
     */
    DeviceApartment() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }

    /**
     * Releases the apartment on the thread that initialized it.
     */
    ~DeviceApartment() {
        winrt::uninit_apartment();
    }
};

} // namespace

void annotate_camera_positions(
    std::vector<CameraDevice>& devices
) {
    using namespace winrt::Windows::Devices::Enumeration;
    if (devices.empty())
        return;

    // DeviceInformation can wait on an MTA worker even when the caller owns a UI apartment.
    try {
        std::async(
            std::launch::async,
            [&devices] {
                DeviceApartment apartment;
                const auto cameras = DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get();
                for (auto& device : devices) {
                    const auto id = winrt::to_hstring(device.id);
                    for (const auto& camera : cameras) {
                        const auto current = camera.Id();
                        const auto match = CompareStringOrdinal(
                            id.c_str(),
                            -1,
                            current.c_str(),
                            -1,
                            TRUE
                        );
                        if (match != CSTR_EQUAL)
                            continue;
                        const auto enclosure = camera.EnclosureLocation();
                        if (enclosure) {
                            if (enclosure.Panel() == Panel::Front)
                                device.position = XMAX_CAMERA_POSITION_FRONT;
                            else if (enclosure.Panel() == Panel::Back)
                                device.position = XMAX_CAMERA_POSITION_BACK;
                        }
                        break;
                    }
                }
            }
        ).get();
    } catch (const winrt::hresult_error&) {
        // An unavailable metadata provider does not prevent using the Media Foundation device.
    } catch (const std::system_error&) {
        // Position metadata is optional when the worker cannot be started.
    }
}

} // namespace xmax::detail
#else
namespace xmax::detail {

void annotate_camera_positions(
    std::vector<CameraDevice>&
) {
    // Toolchains without the Windows SDK projection can still enumerate Media Foundation devices.
}

} // namespace xmax::detail
#endif
