//
//  VideoBeautyProcessor.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Render/Beauty/VideoBeautyProcessor.hpp"
#include "Foundation/Media/FrameConverter.hpp"
#if defined(_WIN32) && defined(XMAX_WITH_AGORA)
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <cstring>

namespace xmax::detail {
namespace {
constexpr size_t capacity = 32 * 1024 * 1024;
#pragma pack(push, 1)
struct Request { uint32_t magic = 0x31424D58, version = 1, width = 0, height = 0, stride = 0, length = 0; float smooth, light, sharp, red; };
struct Response { int32_t code; uint32_t width, height, length; };
#pragma pack(pop)
static_assert(sizeof(Request) == 40 && sizeof(Response) == 16);

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Security {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    Security() {
        Handle token; DWORD length = 0;
        require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value), XMAX_MEDIA_ERROR, "Cannot read beauty IPC owner");
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &length);
        std::vector<BYTE> bytes(length);
        require(GetTokenInformation(token.value, TokenUser, bytes.data(), length, &length), XMAX_MEDIA_ERROR, "Cannot read beauty IPC owner");
        LPWSTR sid = nullptr;
        require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid), XMAX_MEDIA_ERROR, "Cannot encode beauty IPC owner");
        std::wstring acl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")"; LocalFree(sid);
        require(ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr),
                XMAX_MEDIA_ERROR, "Cannot protect beauty IPC");
        attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() { if (descriptor) LocalFree(descriptor); }
};
class Worker final : public VideoBeautyProcessor {
    Handle pipe_, mapping_, process_, job_;
    BYTE* memory_ = nullptr;
    BeautyOptions options_;
    void transfer(void* data, DWORD size, bool write, DWORD timeout = 700) {
        auto at = static_cast<BYTE*>(data);
        const auto end = GetTickCount64() + timeout;
        while (size) {
            Handle event; event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            require(event.value != nullptr, XMAX_MEDIA_ERROR, "Cannot allocate beauty IPC event");
            OVERLAPPED operation{}; operation.hEvent = event.value; DWORD count = 0;
            BOOL done = write ? WriteFile(pipe_.value, at, size, &count, &operation) : ReadFile(pipe_.value, at, size, &count, &operation);
            if (!done && GetLastError() == ERROR_IO_PENDING) {
                const auto now = GetTickCount64();
                const auto remaining = now < end ? static_cast<DWORD>(end - now) : 0;
                if (WaitForSingleObject(event.value, remaining) != WAIT_OBJECT_0) {
                    CancelIoEx(pipe_.value, &operation);
                    GetOverlappedResult(pipe_.value, &operation, &count, TRUE);
                    throw Error(XMAX_MEDIA_ERROR, "The result beauty helper timed out");
                }
                done = GetOverlappedResult(pipe_.value, &operation, &count, FALSE);
            }
            require(done && count, XMAX_MEDIA_ERROR, "The result beauty helper disconnected");
            at += count; size -= count;
        }
    }
    Response send(Request request, DWORD timeout = 700) {
        transfer(&request, sizeof(request), true, timeout);
        Response response{}; transfer(&response, sizeof(response), false, timeout);
        require(response.code == 0, XMAX_MEDIA_ERROR, "Result beauty processing failed (" + std::to_string(response.code) + ")");
        return response;
    }
    void stop() noexcept {
        if (process_.value) {
            if (WaitForSingleObject(process_.value, 100) != WAIT_OBJECT_0) {
                TerminateProcess(process_.value, 1); WaitForSingleObject(process_.value, 1000);
            }
        }
        if (memory_) { UnmapViewOfFile(memory_); memory_ = nullptr; }
    }
  public:
    explicit Worker(BeautyOptions options) : options_(options) {
        try {
            HMODULE module = nullptr;
            require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&make_video_beauty_processor), &module), XMAX_MEDIA_ERROR, "Cannot locate beauty runtime");
            std::wstring path(32768, L'\0'); DWORD size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            require(size && size < path.size(), XMAX_MEDIA_ERROR, "Cannot locate beauty runtime"); path.resize(size);
            auto directory = path.substr(0, path.find_last_of(L"\\/"));
            path = directory + L"\\Xmax.VideoBeautyWorker.exe";
            require(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES, XMAX_MEDIA_ERROR, "Xmax.VideoBeautyWorker.exe is missing");
            GUID guid{}; require(SUCCEEDED(CoCreateGuid(&guid)), XMAX_MEDIA_ERROR, "Cannot allocate beauty IPC identity");
            wchar_t identity[40]{}; StringFromGUID2(guid, identity, 40);
            std::wstring name = L"Xmax.VideoBeauty." + std::wstring(identity), pipeName = L"\\\\.\\pipe\\" + name, mapName = L"Local\\" + name;
            Security security;
            mapping_.value = CreateFileMappingW(INVALID_HANDLE_VALUE, &security.attributes, PAGE_READWRITE, 0, static_cast<DWORD>(capacity * 2), mapName.c_str());
            require(mapping_.value != nullptr, XMAX_MEDIA_ERROR, "Cannot allocate beauty shared memory");
            memory_ = static_cast<BYTE*>(MapViewOfFile(mapping_.value, FILE_MAP_ALL_ACCESS, 0, 0, capacity * 2));
            require(memory_ != nullptr, XMAX_MEDIA_ERROR, "Cannot map beauty shared memory");
            pipe_.value = CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, &security.attributes);
            require(pipe_.value != INVALID_HANDLE_VALUE, XMAX_MEDIA_ERROR, "Cannot create private beauty pipe");
            job_.value = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            require(job_.value && SetInformationJobObject(job_.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), XMAX_MEDIA_ERROR, "Cannot contain beauty process");
            auto command = L"\"" + path + L"\" \"" + pipeName + L"\" \"" + mapName + L"\"";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION info{};
            require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                    nullptr, directory.c_str(), &startup, &info), XMAX_MEDIA_ERROR, "Cannot start beauty helper");
            process_.value = info.hProcess; Handle thread; thread.value = info.hThread;
            require(AssignProcessToJobObject(job_.value, process_.value), XMAX_MEDIA_ERROR, "Cannot contain beauty helper");
            require(ResumeThread(thread.value) != static_cast<DWORD>(-1), XMAX_MEDIA_ERROR, "Cannot resume beauty helper");
            Handle connected; connected.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            OVERLAPPED connection{}; connection.hEvent = connected.value;
            BOOL ready = ConnectNamedPipe(pipe_.value, &connection); DWORD error = ready ? ERROR_SUCCESS : GetLastError();
            if (!ready && error == ERROR_IO_PENDING) {
                if (WaitForSingleObject(connected.value, 5000) != WAIT_OBJECT_0) {
                    CancelIoEx(pipe_.value, &connection); DWORD ignored = 0; GetOverlappedResult(pipe_.value, &connection, &ignored, TRUE);
                    throw Error(XMAX_MEDIA_ERROR, "The result beauty helper did not connect");
                }
                DWORD ignored = 0; ready = GetOverlappedResult(pipe_.value, &connection, &ignored, FALSE);
            } else if (error == ERROR_PIPE_CONNECTED) ready = TRUE;
            require(ready, XMAX_MEDIA_ERROR, "Cannot connect beauty helper");
            Response hello{}; transfer(&hello, sizeof(hello), false, 5000);
            require(hello.code == 0, XMAX_MEDIA_ERROR, "Beauty engine could not initialize (" + std::to_string(hello.code) + ")");
            Request request{}; request.smooth = options.smoothness; request.light = options.lightening; request.sharp = options.sharpness; request.red = options.redness;
            send(request, 3000);
        } catch (...) { stop(); throw; }
    }
    ~Worker() override { stop(); }
    void update(BeautyOptions options) override {
        validate_beauty(options);
        Request request{}; request.smooth = options.smoothness; request.light = options.lightening;
        request.sharp = options.sharpness; request.red = options.redness;
        send(request, 3000); options_ = options;
    }
    std::shared_ptr<Frame> process(const Frame& source) override {
        auto frame = bgra_frame(source);
        const auto length = static_cast<uint64_t>(frame.width) * frame.height * 4;
        require(length <= capacity && frame.width <= 4096 && frame.height <= 2160, XMAX_MEDIA_ERROR, "Beauty frame exceeds shared-memory capacity");
        for (uint32_t y = 0; y < frame.height; ++y)
            std::memcpy(memory_ + static_cast<size_t>(y) * frame.width * 4, frame.planes[0].data() + y * frame.strides[0], frame.width * 4);
        Request request{}; request.width = frame.width; request.height = frame.height; request.stride = frame.width * 4; request.length = static_cast<uint32_t>(length);
        request.smooth = options_.smoothness; request.light = options_.lightening; request.sharp = options_.sharpness; request.red = options_.redness;
        auto response = send(request);
        require(response.width == frame.width && response.height == frame.height && response.length == length,
                XMAX_MEDIA_ERROR, "Beauty output dimensions changed");
        frame.strides[0] = frame.width * 4; frame.planes[0].resize(static_cast<size_t>(length));
        std::memcpy(frame.planes[0].data(), memory_ + capacity, static_cast<size_t>(length));
        return std::make_shared<Frame>(std::move(frame));
    }
};
}
std::unique_ptr<VideoBeautyProcessor> make_video_beauty_processor(BeautyOptions options) {
    validate_beauty(options);
    return std::make_unique<Worker>(options);
}
}
#else
namespace xmax::detail {
std::unique_ptr<VideoBeautyProcessor> make_video_beauty_processor(BeautyOptions options) {
    validate_beauty(options);
    throw Error(XMAX_MEDIA_ERROR, "Result beauty is unavailable in this SDK build");
}
}
#endif
