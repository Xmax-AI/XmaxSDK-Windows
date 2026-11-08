//
//  WindowsRecordingEndpoint.hpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <string>
#include <windows.h>
#include <audioclient.h>
#include <wrl/client.h>

namespace xmax::detail {

void check_microphone(HRESULT result, const char* message);
std::string default_recording_endpoint_id();

// The caller owns a COM apartment for the returned audio client's lifetime.
Microsoft::WRL::ComPtr<IAudioClient> open_recording_endpoint(const std::string& device);

} // namespace xmax::detail
