//
//  main.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

// XmaxSDK private local-only result beauty worker. Agora SDK 4.6.2; see THIRD_PARTY_NOTICES.txt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <IAgoraService.h>
#include <NGIAgoraMediaNodeFactory.h>
#include <NGIAgoraMediaNode.h>
#include <NGIAgoraVideoTrack.h>
#include <AgoraRefCountedObject.h>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cmath>
using namespace agora;
#pragma pack(push, 1)
struct Request { uint32_t magic, version, width, height, stride, length; float smooth, light, sharp, red; };
struct Response { int32_t code; uint32_t width, height, length; };
#pragma pack(pop)
static_assert(sizeof(Request)==40 && sizeof(Response)==16);
constexpr size_t Capacity = 32 * 1024 * 1024;
static bool Transfer(HANDLE pipe, void* data, DWORD bytes, bool writing) {
    auto at = static_cast<BYTE*>(data);
    while(bytes) { DWORD count=0; if (!(writing ? WriteFile(pipe,at,bytes,&count,nullptr) : ReadFile(pipe,at,bytes,&count,nullptr)) || !count) return false; at+=count; bytes-=count; }
    return true;
}
class Sink : public rtc::IVideoFilter {
public:
    std::mutex mutex; std::condition_variable ready; bool waiting=false, complete=false;
    uint32_t width=0,height=0; int64_t expectedTimestamp=0; std::vector<uint8_t> pixels;
    bool adaptVideoFrame(const media::base::VideoFrame& frame, media::base::VideoFrame& adapted) override {
        adapted=frame; std::lock_guard<std::mutex> lock(mutex);
        if(!waiting || (frame.renderTimeMs && frame.renderTimeMs<expectedTimestamp)) return true;
        waiting=false; complete=true; width=height=0;
        if((frame.type==media::base::VIDEO_PIXEL_I420 || frame.type==media::base::VIDEO_PIXEL_DEFAULT) && frame.width>0 && frame.height>0 && frame.yBuffer && frame.uBuffer && frame.vBuffer && frame.yStride>=frame.width && frame.uStride>=frame.width/2 && frame.vStride>=frame.width/2 && uint64_t(frame.width)*frame.height*4<=Capacity) {
            width=frame.width; height=frame.height; pixels.resize(size_t(width)*height*4);
            auto byte=[](int v) {return uint8_t(std::clamp(v,0,255));};
            for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
                int l=298*(int(frame.yBuffer[y*frame.yStride+x])-16), u=int(frame.uBuffer[(y/2)*frame.uStride+x/2])-128, v=int(frame.vBuffer[(y/2)*frame.vStride+x/2])-128;
                auto at=(size_t(y)*width+x)*4;
                pixels[at]=byte((l+541*u+128)>>8); pixels[at+1]=byte((l-55*u-136*v+128)>>8); pixels[at+2]=byte((l+459*v+128)>>8); pixels[at+3]=255;
            }
        }
        ready.notify_one(); return true;
    }
};
class Renderer : public rtc::IVideoSinkBase {
public:
    agora_refptr<Sink> sink;
    explicit Renderer(agora_refptr<Sink> target):sink(target){}
    int onFrame(const media::base::VideoFrame& frame) override { auto adapted=frame; sink->adaptVideoFrame(frame,adapted); return 0; }
};
struct Engine {
    base::IAgoraService* service=nullptr;
    agora_refptr<rtc::IMediaNodeFactory> factory; agora_refptr<rtc::IVideoFrameSender> sender;
    agora_refptr<rtc::ILocalVideoTrack> track; agora_refptr<Sink> sink; agora_refptr<Renderer> renderer; int64_t timestamp=0;
    Engine() {
        service=createAgoraService(); if(!service) throw -10;
        base::AgoraServiceConfiguration config; config.appId="5588dfe2e7874bd08ea28f4968ec2d40"; config.enableVideo=true; config.enableAudioDevice=false; config.enableAudioProcessor=false;
        if(service->initialize(config)!=0) throw -11;
        service->loadExtensionProvider("libagora_clear_vision_extension.dll");
        if(service->enableExtension("agora_video_filters_clear_vision","clear_vision","xmax-result-beauty",true)!=0) throw -12;
        factory=service->createMediaNodeFactory(); if(factory) sender=factory->createVideoFrameSender();
        if(sender) track=service->createCustomVideoTrack(sender,"xmax-result-beauty");
        if(!track) throw -13;
        sink=new RefCountedObject<Sink>();
        renderer=new RefCountedObject<Renderer>(sink);
        if(!track->addRenderer(renderer,media::base::POSITION_PRE_RENDERER) || track->setEnabled(true)!=0) throw -14;
    }
    ~Engine() { if(track) {track->setEnabled(false); if(renderer) track->removeRenderer(renderer,media::base::POSITION_PRE_RENDERER);} track=nullptr; renderer=nullptr; sink=nullptr; sender=nullptr; factory=nullptr; if(service) service->release(); }
    int Configure(const Request& r) {
        for(float value : {r.smooth,r.light,r.sharp,r.red}) if(!std::isfinite(value)||value<0||value>1) return -20;
        std::ostringstream json; json << "{\"enable\":true,\"smoothness\":" << r.smooth << ",\"lightness\":" << r.light << ",\"sharpness\":" << r.sharp << ",\"redness\":" << r.red << ",\"contrast\":1}";
        if(track->setFilterProperty(service->getExtensionId("agora_video_filters_clear_vision","clear_vision"),"beauty_effect_option",json.str().c_str())!=0) return -21;
        return 0;
    }
    int Process(const Request& r, BYTE* memory, Response& response) {
        {std::lock_guard<std::mutex> lock(sink->mutex); sink->waiting=true; sink->complete=false;}
        media::base::ExternalVideoFrame frame; frame.type=media::base::ExternalVideoFrame::VIDEO_BUFFER_RAW_DATA;
        frame.format=media::base::VIDEO_PIXEL_BGRA; frame.buffer=memory; frame.stride=r.width; frame.height=r.height; frame.timestamp=timestamp=std::max<int64_t>(timestamp+1,GetTickCount64());
        {std::lock_guard<std::mutex> lock(sink->mutex); sink->expectedTimestamp=timestamp;}
        frame.colorSpace.primaries=media::base::ColorSpace::PRIMARYID_BT709; frame.colorSpace.transfer=media::base::ColorSpace::TRANSFERID_BT709; frame.colorSpace.matrix=media::base::ColorSpace::MATRIXID_BT709; frame.colorSpace.range=media::base::ColorSpace::RANGEID_FULL;
        int sent=sender->sendVideoFrame(frame);
        std::unique_lock<std::mutex> lock(sink->mutex);
        if(sent!=0) {sink->waiting=false; return sent;}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(450);
        while(!sink->complete && std::chrono::steady_clock::now()<deadline) {
            if(sink->ready.wait_for(lock,std::chrono::milliseconds(20),[&]{return sink->complete;})) break;
            // Windows starts its local video graph asynchronously. Prime it with the same owned
            // frame until the first result is delivered; subsequent frames normally need one send.
            lock.unlock(); sent=sender->sendVideoFrame(frame); lock.lock();
            if(sent!=0) {sink->waiting=false; return sent;}
        }
        if(!sink->complete) {sink->waiting=false; return -22;}
        if(!sink->width || sink->pixels.size()>Capacity) return -23;
        memcpy(memory+Capacity,sink->pixels.data(),sink->pixels.size()); response.width=sink->width; response.height=sink->height; response.length=uint32_t(sink->pixels.size()); return 0;
    }
};
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]); if(!mapping) return 3;
    auto memory=static_cast<BYTE*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,Capacity*2)); if(!memory){CloseHandle(mapping);return 4;}
    WaitNamedPipeW(argv[1],3000); HANDLE pipe=CreateFileW(argv[1],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE){UnmapViewOfFile(memory);CloseHandle(mapping);return 5;}
    try {
        Engine engine; Response hello{}; if(!Transfer(pipe,&hello,sizeof(hello),true)) throw -30;
        Request request{};
        while(Transfer(pipe,&request,sizeof(request),false)) {
            Response response{};
            if(request.magic!=0x31424D58 || request.version!=1) response.code=-31;
            else if(request.width==0 && request.height==0 && request.length==0) response.code=engine.Configure(request);
            else if(!request.width || !request.height || request.width>4096 || request.height>2160 || request.width%2 || request.height%2 || request.stride!=request.width*4 || uint64_t(request.stride)*request.height!=request.length || request.length>Capacity) response.code=-31;
            else response.code=engine.Process(request,memory,response);
            if(!Transfer(pipe,&response,sizeof(response),true)) break;
        }
    } catch(int code) {Response failure{code,0,0,0}; Transfer(pipe,&failure,sizeof(failure),true);} catch(...) {Response failure{-99,0,0,0}; Transfer(pipe,&failure,sizeof(failure),true);}
    CloseHandle(pipe); UnmapViewOfFile(memory); CloseHandle(mapping); return 0;
}
