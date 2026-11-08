//
//  MicrophoneDevices.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/Audio/MicrophoneDevices.hpp"
#ifdef _WIN32
#include <windows.h>
#include "Foundation/Media/Audio/WindowsRecordingEndpoint.hpp"
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wrl/client.h>
#endif

namespace xmax::detail {
#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

class ComApartment {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        require(SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE,
                XMAX_MEDIA_ERROR, "Cannot initialize COM for microphone discovery");
    }

    ~ComApartment() {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }

  private:
    HRESULT result_;
};

ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> result;
    constexpr GUID device_enumerator{0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
    check_microphone(CoCreateInstance(device_enumerator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&result)),
                     "Cannot open the Windows recording endpoint manager");
    return result;
}

std::string utf8(const wchar_t* text) {
    if (!text || !*text)
        return {};
    const auto length = int(wcslen(text));
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string result(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring wide(const std::string& text) {
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
    require(length > 0, XMAX_INVALID_ARGUMENT, "Invalid recording endpoint identifier");
    std::wstring result(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), length);
    return result;
}

std::string endpoint_id(IMMDevice* device) {
    LPWSTR id = nullptr;
    check_microphone(device->GetId(&id), "Cannot read the recording endpoint identifier");
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(id, CoTaskMemFree);
    return utf8(owner.get());
}

std::string endpoint_name(IMMDevice* device) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties)))
        return {};
    constexpr PROPERTYKEY friendly_name{{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    PROPVARIANT value{};
    if (FAILED(properties->GetValue(friendly_name, &value)))
        return {};
    struct ClearProperty {
        PROPVARIANT& value;
        ~ClearProperty() { PropVariantClear(&value); }
    } cleanup{value};
    return value.vt == VT_LPWSTR ? utf8(value.pwszVal) : std::string{};
}

} // namespace
#endif

std::vector<CameraDevice> list_microphone_devices() {
#ifdef _WIN32
    ComApartment apartment;
    auto manager = enumerator();
    ComPtr<IMMDevice> default_device;
    const auto default_id = SUCCEEDED(manager->GetDefaultAudioEndpoint(eCapture, eConsole, &default_device))
        ? endpoint_id(default_device.Get()) : std::string{};
    ComPtr<IMMDeviceCollection> devices;
    check_microphone(manager->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices),
                     "Cannot enumerate recording endpoints");
    UINT count = 0;
    check_microphone(devices->GetCount(&count), "Cannot count recording endpoints");
    std::vector<CameraDevice> result;
    result.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(index, &device)))
            continue;
        const auto id = endpoint_id(device.Get());
        auto name = endpoint_name(device.Get());
        result.push_back({id, name.empty() ? id : std::move(name), id == default_id});
    }
    return result;
#else
    return {};
#endif
}

#ifdef _WIN32
void check_microphone(HRESULT result, const char* message) {
    if (FAILED(result))
        throw Error(result == E_ACCESSDENIED ? XMAX_MICROPHONE_PERMISSION_DENIED : XMAX_MEDIA_ERROR,
                    std::string(message) + " (HRESULT " + std::to_string(uint32_t(result)) + ")");
}

std::string default_recording_endpoint_id() {
    ComPtr<IMMDevice> endpoint;
    check_microphone(enumerator()->GetDefaultAudioEndpoint(eCapture, eConsole, &endpoint),
                     "The system default microphone is unavailable");
    return endpoint_id(endpoint.Get());
}

ComPtr<IAudioClient> open_recording_endpoint(const std::string& device) {
    auto manager = enumerator();
    ComPtr<IMMDevice> endpoint;
    check_microphone(device.empty()
                         ? manager->GetDefaultAudioEndpoint(eCapture, eConsole, &endpoint)
                         : manager->GetDevice(wide(device).c_str(), &endpoint),
                     "The selected recording endpoint is unavailable");

    ComPtr<IMMEndpoint> direction;
    check_microphone(endpoint.As(&direction), "Cannot inspect the recording endpoint");
    EDataFlow flow = eAll;
    check_microphone(direction->GetDataFlow(&flow), "Cannot inspect recording endpoint direction");
    require(flow == eCapture, XMAX_INVALID_ARGUMENT, "The selected endpoint is not a microphone");

    ComPtr<IAudioClient> audio;
    check_microphone(endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(audio.GetAddressOf())),
                     "Cannot activate the recording endpoint");
    return audio;
}
#endif

void validate_microphone_device(const std::string& device) {
#ifdef _WIN32
    ComApartment apartment;
    auto audio = open_recording_endpoint(device);
    WAVEFORMATEX* format = nullptr;
    check_microphone(audio->GetMixFormat(&format), "Cannot read the recording format");
    const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> owner(format, CoTaskMemFree);
    check_microphone(audio->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, owner.get(), nullptr),
                     "Cannot access the recording endpoint");
#else
    (void)device;
#endif
}

} // namespace xmax::detail
