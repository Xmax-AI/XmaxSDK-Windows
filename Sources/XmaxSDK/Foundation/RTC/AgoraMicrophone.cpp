//
//  AgoraMicrophone.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/RTC/AgoraMicrophone.hpp"
#include <IAgoraRtcEngine.h>

namespace xmax::detail {
namespace {

using agora::rtc::IRtcEngine;

void checked(int result, const char* operation) {
    if (result < 0)
        throw Error(XMAX_RTC_ERROR, std::string(operation) + " failed (" + std::to_string(result) + ")");
}

void select_device(IRtcEngine& engine, const std::string& device) {
    agora::rtc::AAudioDeviceManager devices(&engine);
    require(bool(devices), XMAX_RTC_ERROR, "Microphone device manager is unavailable");
    checked(devices->followSystemRecordingDevice(device.empty()), "followSystemRecordingDevice");
    if (!device.empty())
        checked(devices->setRecordingDevice(device.c_str()), "setRecordingDevice");
}

} // namespace

void AgoraMicrophone::select(
    IRtcEngine* active_engine,
    const std::string& device
) {
    if (active_engine && enabled_)
        select_device(*active_engine, device);
    device_ = device;
}

void AgoraMicrophone::prepare(
    IRtcEngine& engine,
    bool enabled
) {
    if (enabled)
        select_device(engine, device_);
    available_ = false;
    enabled_ = enabled;
    checked(engine.enableAudioVolumeIndication(200, 3, false), "enableAudioVolumeIndication");
    checked(engine.enableLocalAudio(enabled), "enableLocalAudio");
}

void AgoraMicrophone::enable(
    IRtcEngine& engine,
    bool enabled
) {
    if (enabled == enabled_)
        return;

    agora::rtc::ChannelMediaOptions options;
    options.publishMicrophoneTrack = enabled;
    if (enabled) {
        select_device(engine, device_);
        available_ = false;
        checked(engine.enableLocalAudio(true), "enableLocalAudio");
        const auto result = engine.updateChannelMediaOptions(options);
        if (result < 0)
            engine.enableLocalAudio(false);
        checked(result, "publishMicrophoneTrack");
        enabled_ = true;
    } else {
        enabled_ = false;
        available_ = false;
        const auto publication = engine.updateChannelMediaOptions(options);
        const auto capture = engine.enableLocalAudio(false);
        checked(publication, "unpublishMicrophoneTrack");
        checked(capture, "disableLocalAudio");
    }
}

} // namespace xmax::detail
