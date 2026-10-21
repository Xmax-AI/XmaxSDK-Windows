//
//  MediaFoundationCamera.cpp
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Logging/XmaxLogger.hpp"
#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
// clang-format off
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
// clang-format on
#include <mutex>

namespace xmax::detail {
namespace {

/**
 * 校验 HRESULT，保留错误码并记录摄像头访问被拒绝的诊断。
 *
 * @param[in] result 需校验的 HRESULT，失败时保留其数值并抛出媒体错误。
 * @param[in] operation 当前原生摄像头操作的诊断名称。
 */
void checked(
    HRESULT result,
    const char* operation
) {
    if (result == E_ACCESSDENIED)
        XmaxLogger::write(LogCategory::Permission, XMAX_LOG_ERROR, [](auto language) {
            return XmaxLogger::localized(
                language, "摄像头访问被系统拒绝 (Camera Access Denied)", "Camera Access Denied"
            );
        });
    require(
        SUCCEEDED(result), result == E_ACCESSDENIED ? XMAX_CAMERA_PERMISSION_DENIED : XMAX_MEDIA_ERROR,
        std::string(operation) + " failed (HRESULT " + std::to_string(uint32_t(result)) + ")"
    );
}

/**
 * 以独占所有权管理一个 COM 接口引用；移动操作转移引用而不增加引用计数。
 *
 * @tparam T 当前所有者管理的句柄或接口类型。
 */
template <class T> class Com {

    /**
     * 独占拥有的 COM 接口引用；为空时不持有资源。
     */
    T* value_ = nullptr;

  public:

    /**
     * 创建空 COM 引用所有者，不持有任何接口引用。
     */
    Com() = default;

    /**
     * 释放本对象独占拥有的 COM 接口引用。
     */
    ~Com() {
        if (value_)
            value_->Release();
    }

    /**
     * 禁止复制构造，避免同一资源被多个独占所有者重复释放。
     *
     * @param[in] other 复制源对象；此操作被禁止，不得调用。
     */
    Com(
        const Com& other
    ) = delete;

    /**
     * 禁止复制赋值，避免同一资源被多个独占所有者重复释放。
     *
     * @param[in] other 复制源对象；此操作被禁止，不得调用。
     */
    Com& operator=(
        const Com& other
    ) = delete;

    /**
     * 接管源对象的资源所有权，移动后源对象为空。
     *
     * @param[in,out] other 引用所有权的源对象，移动完成后源对象为空。
     */
    Com(
        Com&& other
    ) noexcept
        : value_(std::exchange(other.value_, nullptr)) {
    }

    /**
     * 释放当前 COM 引用并接管源对象的引用。
     *
     * @param[in,out] other 引用所有权的源对象，移动完成后源对象为空；允许自移动赋值。
     * @return 当前 COM 引用所有者的引用。
     */
    Com& operator=(
        Com&& other
    ) noexcept {
        if (this != &other) {
            if (value_)
                value_->Release();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    /**
     * 借用当前 COM 接口指针，不增加引用计数。
     *
     * @return 借用的 COM 接口指针；未持有接口时为 nullptr。
     */
    T* get() const {
        return value_;
    }

    /**
     * 借用当前 COM 接口进行成员访问，不转移所有权。
     *
     * @return 借用的 COM 接口指针，不增加引用计数。
     */
    T* operator->() const {
        return value_;
    }

    /**
     * 提供用于接收已拥有 COM 引用的空指针槽位。
     *
     * @return 接收一个已拥有 COM 引用的指针槽位；调用前本对象必须为空。
     */
    T** put() {
        return &value_;
    }
};

/**
 * 在当前线程配对管理 COM 与 Media Foundation 的初始化和释放。
 */
struct Platform {

    /**
     * 当前线程是否需要在销毁时配对调用 CoUninitialize。
     */
    bool uninitialize = false;

    /**
     * 是否已成功启动 Media Foundation，决定销毁时是否执行关闭。
     */
    bool started = false;

    /**
     * 初始化当前线程的 COM 与 Media Foundation，部分失败时回滚已完成步骤。
     */
    Platform() {
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        require(SUCCEEDED(result) || result == RPC_E_CHANGED_MODE, XMAX_MEDIA_ERROR, "Cannot initialize COM");
        uninitialize = SUCCEEDED(result);

        const auto startup = MFStartup(MF_VERSION, MFSTARTUP_FULL);

        if (FAILED(startup)) {
            if (uninitialize)
                CoUninitialize();
            checked(startup, "MFStartup");
        }

        started = true;
    }

    /**
     * 在初始化所属线程关闭 Media Foundation，并配对释放 COM 初始化。
     */
    ~Platform() {
        if (started)
            MFShutdown();
        if (uninitialize)
            CoUninitialize();
    }
};

/**
 * 将具有明确长度的 Windows UTF-16 文本转换为 UTF-8。
 *
 * @param[in] value 具有 length 个 UTF-16 码元的只读 Windows 字符串。
 * @param[in] length 输入 UTF-16 码元数，不包含未显式传入的结尾 NUL。
 */
std::string utf8(
    const wchar_t* value,
    UINT32 length
) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, int(length), nullptr, 0, nullptr, nullptr);
    std::string text(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, int(length), text.data(), count, nullptr, nullptr);

    return text;
}

/**
 * 读取 Media Foundation 字符串属性，并释放系统分配的原生存储。
 *
 * @param[in] value 保存原生属性的 Media Foundation 对象，调用期间有效。
 * @param[in] key 要读取的字符串属性 GUID。
 */
std::string attribute(
    IMFAttributes* value,
    REFGUID key
) {
    wchar_t* text = nullptr;
    UINT32 count = 0;
    checked(value->GetAllocatedString(key, &text, &count), "Read camera identity");

    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(text, CoTaskMemFree);

    return utf8(text, count);
}

/**
 * 拥有 Media Foundation 摄像头激活对象数组，并负责释放数组及接口引用。
 */
struct Devices {

    /**
     * 系统分配的设备激活对象数组；数组及其中的 COM 引用由本对象释放。
     */
    IMFActivate** values = nullptr;

    /**
     * 系统分配的设备激活对象数量。
     */
    UINT32 count = 0;

    /**
     * 枚举并接管摄像头激活对象及其系统分配的数组。
     */
    Devices() {
        Com<IMFAttributes> attributes;
        checked(MFCreateAttributes(attributes.put(), 1), "Camera enumeration attributes");
        checked(
            attributes->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
            ),
            "Camera source type"
        );
        checked(MFEnumDeviceSources(attributes.get(), &values, &count), "Enumerate camera devices");
    }

    /**
     * 释放摄像头激活接口引用及系统分配的数组。
     */
    ~Devices() {
        for (UINT32 i = 0; i < count; ++i)
            values[i]->Release();
        CoTaskMemFree(values);
    }
};

/**
 * 保存异步样本读取器、协商后的像素布局和采集回调。
 */
struct CaptureState {

    /**
     * 保护读取器、媒体类型及停止状态，并串行交付样本的互斥锁。
     */
    std::mutex mutex;

    /**
     * 当前采集会话拥有的异步样本读取器。
     */
    Com<IMFSourceReader> reader;

    /**
     * 当前采集会话的帧和错误交付回调。
     */
    CaptureCallbacks callbacks;

    /**
     * 采集是否已进入停止或失败状态，禁止继续请求和交付样本。
     */
    bool stopping = false;

    /**
     * 视频宽度，单位为像素。
     */
    UINT32 width = 0;

    /**
     * 视频高度，单位为像素。
     */
    UINT32 height = 0;

    /**
     * 协商后的 RGB 行步幅，单位为字节；负值表示反向行布局。
     */
    LONG stride = 0;

    /**
     * 读取并校验协商后的像素尺寸与 RGB 字节步幅。
     */
    void type() {
        Com<IMFMediaType> output;
        checked(
            reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, output.put()),
            "Read camera output type"
        );
        checked(
            MFGetAttributeSize(output.get(), MF_MT_FRAME_SIZE, &width, &height), "Read camera dimensions"
        );
        require(
            width >= 2 && height >= 2 && uint64_t(width) * height <= 2100000, XMAX_MEDIA_ERROR,
            "Camera output dimensions exceed the frame budget"
        );

        UINT32 raw = 0;

        if (SUCCEEDED(output->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw)))
            stride = LONG(raw);
        else
            checked(
                MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride),
                "Read camera stride"
            );
    }

    /**
     * 按原生样本的行步幅复制 BGRA 像素，返回独立拥有的帧。
     *
     * @param[in] sample 当前原生视频样本，借用有效期仅限回调或同步复制期间。
     */
    Frame copy(
        IMFSample* sample
    ) {
        Com<IMFMediaBuffer> buffer;
        checked(sample->ConvertToContiguousBuffer(buffer.put()), "Read camera sample");

        Frame frame;
        frame.width = width;
        frame.height = height;
        frame.timestamp_us =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
        frame.strides[0] = size_t(width) * 4;
        frame.planes[0].resize(frame.strides[0] * height);

        Com<IMF2DBuffer> plane;
        BYTE* pixels = nullptr;
        LONG pitch = stride;

        if (SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer, reinterpret_cast<void**>(plane.put())))) {
            checked(plane->Lock2D(&pixels, &pitch), "Lock camera image");

            if (size_t(pitch < 0 ? -int64_t(pitch) : pitch) < frame.strides[0]) {
                plane->Unlock2D();
                throw Error(XMAX_MEDIA_ERROR, "Camera row stride is shorter than its width");
            }

            for (UINT32 y = 0; y < height; ++y)
                std::memcpy(
                    frame.planes[0].data() + y * frame.strides[0], pixels + ptrdiff_t(y) * pitch,
                    frame.strides[0]
                );

            plane->Unlock2D();
        } else {
            DWORD size = 0;
            checked(buffer->Lock(&pixels, nullptr, &size), "Lock camera buffer");

            const size_t row = size_t(pitch < 0 ? -int64_t(pitch) : pitch);

            if (row < frame.strides[0] || uint64_t(row) * (height - 1) + frame.strides[0] > size) {
                buffer->Unlock();
                throw Error(XMAX_MEDIA_ERROR, "Camera buffer is shorter than its layout");
            }

            if (pitch < 0)
                pixels += row * (height - 1);

            for (UINT32 y = 0; y < height; ++y)
                std::memcpy(
                    frame.planes[0].data() + y * frame.strides[0], pixels + ptrdiff_t(y) * pitch,
                    frame.strides[0]
                );

            buffer->Unlock();
        }

        auto* owned_pixels = frame.planes[0].data();

        for (size_t i = 3; i < frame.planes[0].size(); i += 4)
            owned_pixels[i] = 255;

        return frame;
    }
};

/**
 * 实现 Media Foundation 异步读取回调，以弱引用访问采集状态。
 */
class ReaderCallback final : public IMFSourceReaderCallback {

    /**
     * 当前 COM 回调对象的原子引用计数。
     */
    std::atomic<ULONG> references_{1};

    /**
     * 对采集状态的弱引用，避免读取器与回调形成所有权环。
     */
    std::weak_ptr<CaptureState> state_;

  public:

    /**
     * 保留采集状态的弱引用，避免读取器和回调形成所有权环。
     */
    explicit ReaderCallback(
        std::shared_ptr<CaptureState> state
    )
        : state_(state) {
    }

    /**
     * 查询当前对象支持的 IUnknown 或样本读取回调接口。
     *
     * @param[in] iid 待查询的 COM 接口标识。
     * @param[out] out 接收已增加引用计数的 COM 接口指针；查询失败时置空。
     * @return 成功返回 S_OK；输出指针为空返回 E_POINTER，不支持的接口返回 E_NOINTERFACE。
     */
    STDMETHODIMP QueryInterface(
        REFIID iid,
        void** out
    ) override {
        if (!out)
            return E_POINTER;

        *out = nullptr;

        if (iid == IID_IUnknown || iid == IID_IMFSourceReaderCallback) {
            *out = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    /**
     * 原子增加当前 COM 回调对象的引用计数。
     *
     * @return 递增后的 COM 引用计数。
     */
    STDMETHODIMP_(ULONG) AddRef() override {
        return ++references_;
    }

    /**
     * 释放一个回调引用，并在引用计数归零时销毁对象。
     *
     * @return 递减后的 COM 引用计数；0 表示对象已销毁。
     */
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }

    /**
     * 处理读取结果、媒体类型变化与错误，然后请求下一样本。
     *
     * @param[in] status 异步读取结果 HRESULT，失败时终止采集并报告媒体错误。
     * @param[in] stream_index 触发回调的媒体流索引。
     * @param[in] flags Media Foundation 样本读取状态标记。
     * @param[in] timestamp Media Foundation 样本时间戳，单位为 100 纳秒；SDK 使用自己的单调时钟排序帧。
     * @param[in] sample 当前原生视频样本，借用有效期仅限回调或同步复制期间。
     * @return 返回 S_OK；读取失败通过采集错误回调报告。
     */
    STDMETHODIMP OnReadSample(
        HRESULT status,
        [[maybe_unused]] DWORD stream_index,
        DWORD flags,
        [[maybe_unused]] LONGLONG timestamp,
        IMFSample* sample
    ) override {
        auto state = state_.lock();

        if (!state)
            return S_OK;

        std::lock_guard<std::mutex> lock(state->mutex);

        if (state->stopping)
            return S_OK;

        try {
            checked(status, "Camera capture");
            require(!(flags & MF_SOURCE_READERF_ENDOFSTREAM), XMAX_MEDIA_ERROR, "Camera stream ended");

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                state->type();

            if (sample)
                state->callbacks.frame(state->copy(sample));

            checked(
                state->reader->ReadSample(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr
                ),
                "Request camera sample"
            );
        } catch (const std::exception& error) {
            state->stopping = true;
            if (state->callbacks.failure) {
                try {
                    state->callbacks.failure(Error(XMAX_MEDIA_ERROR, error.what()));
                } catch (...) {
                }
            }
        }

        return S_OK;
    }

    /**
     * 确认无需额外处理的样本读取器事件。
     *
     * @param[in] stream_index 触发回调的媒体流索引。
     * @param[in] media_event 原生媒体事件，仅在回调期间借用。
     * @return S_OK，表示事件已处理。
     */
    STDMETHODIMP OnEvent(
        [[maybe_unused]] DWORD stream_index,
        [[maybe_unused]] IMFMediaEvent* media_event
    ) override {
        return S_OK;
    }

    /**
     * 确认停止采集期间的读取器排空事件。
     *
     * @param[in] stream_index 触发回调的媒体流索引。
     * @return S_OK，表示读取器排空已确认。
     */
    STDMETHODIMP OnFlush(
        [[maybe_unused]] DWORD stream_index
    ) override {
        return S_OK;
    }
};

/**
 * 通过 Media Foundation 枚举并采集摄像头，向上层交付连续的 BGRA 帧。
 */
class MfCamera final : public CameraCapture {

    /**
     * 为当前采集生命周期保留的 COM 和 Media Foundation 初始化状态。
     */
    std::unique_ptr<Platform> platform_;

    /**
     * 当前拥有的原生摄像头媒体源。
     */
    Com<IMFMediaSource> source_;

    /**
     * 由读取器回调共享的当前采集状态。
     */
    std::shared_ptr<CaptureState> state_;

  public:

    /**
     * 停止摄像头采集并排空回调，然后释放拥有的媒体资源。
     */
    ~MfCamera() override {
        stop();
    }

    /**
     * 枚举 Media Foundation 摄像头的符号标识和显示名称。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    std::vector<CameraDevice> devices() override {
        Platform platform;
        Devices found;
        std::vector<CameraDevice> result;

        for (UINT32 i = 0; i < found.count; ++i)
            result.push_back(
                {attribute(found.values[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                 attribute(found.values[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME), i == 0}
            );

        return result;
    }

    /**
     * 打开选定摄像头，协商 BGRA 输出并启动异步样本读取。
     *
     * @param[in] id UTF-8 摄像头设备标识，空文本选择默认摄像头。
     * @param[in] format 请求的视频像素宽高及每秒帧数，须符合调用接口的规格限制。
     * @param[in] callbacks 采集帧与失败回调，在原生媒体线程交付，停止返回前排空。
     * @param[in] cancel 协作式取消判断函数；返回 true 时应终止当前操作。
     */
    void start(
        const std::string& id,
        xmax_video_format format,
        CaptureCallbacks callbacks,
        const Cancel& cancel
    ) override {
        stop();
        require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
        platform_ = std::make_unique<Platform>();

        Devices found;
        IMFActivate* selected = nullptr;

        for (UINT32 i = 0; i < found.count; ++i)
            if ((id.empty() && i == 0) ||
                attribute(found.values[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == id) {
                selected = found.values[i];
                break;
            }

        require(selected != nullptr, XMAX_MEDIA_ERROR, "Requested camera is unavailable");

        const auto name = attribute(selected, MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        XmaxLogger::write(LogCategory::Media, XMAX_LOG_INFO, [&](auto language) {
            return XmaxLogger::localized(language, "选择摄像头 (Selected Camera)", "Selected Camera") +
                   "\n└─ " + name;
        });
        checked(
            selected->ActivateObject(IID_IMFMediaSource, reinterpret_cast<void**>(source_.put())),
            "Open camera; check Windows camera permissions"
        );
        state_ = std::make_shared<CaptureState>();
        state_->callbacks = std::move(callbacks);

        Com<IMFAttributes> attributes;
        checked(MFCreateAttributes(attributes.put(), 2), "Capture attributes");

        auto callback = new ReaderCallback(state_);
        const auto result = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback);
        callback->Release();
        checked(result, "Capture callback");
        checked(
            attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE), "Camera RGB conversion"
        );
        checked(
            MFCreateSourceReaderFromMediaSource(source_.get(), attributes.get(), state_->reader.put()),
            "Create camera reader"
        );
        state_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        checked(
            state_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE),
            "Select camera video"
        );

        Com<IMFMediaType> best;
        uint64_t best_score = std::numeric_limits<uint64_t>::max();

        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> type;

            if (FAILED(
                    state_->reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, type.put())
                ))
                break;

            UINT32 w = 0, h = 0, numerator = 0, denominator = 1;

            if (FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h)) || w < 2 || h < 2 || w % 2 ||
                h % 2 || uint64_t(w) * h > 2100000)
                continue;

            MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &numerator, &denominator);

            const double fps = denominator ? double(numerator) / denominator : 0;
            const auto score = uint64_t(
                                   std::abs(int64_t(std::max(w, h)) - std::max(format.width, format.height)) +
                                   std::abs(int64_t(std::min(w, h)) - std::min(format.width, format.height))
                               ) +
                               uint64_t(std::abs(fps - format.fps) * 100);

            if (score < best_score) {
                best_score = score;
                best = std::move(type);
            }
        }

        require(best.get() != nullptr, XMAX_MEDIA_ERROR, "Camera has no supported capture format");
        checked(
            state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best.get()),
            "Select camera capture format"
        );

        Com<IMFMediaType> output;
        checked(MFCreateMediaType(output.put()), "Create RGB camera format");
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        checked(
            state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.get()),
            "Negotiate BGRA camera output"
        );
        state_->type();
        require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
        checked(
            state_->reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr
            ),
            "Start camera capture"
        );
    }

    /**
     * 停止样本交付、解除回调并释放原生摄像头资源。
     */
    void stop() noexcept override {
        if (state_) {
            Com<IMFSourceReader> reader;
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->stopping = true;
                state_->callbacks = {};
                reader = std::move(state_->reader);
            }

            if (reader.get())
                reader->Flush(MF_SOURCE_READER_ALL_STREAMS);

            state_.reset();
        }

        if (source_.get())
            source_->Shutdown();

        source_ = {};
        platform_.reset();
    }
};
} // namespace

/**
 * 检查当前 SDK 构建是否提供原生摄像头采集后端。
 */
bool camera_available() noexcept {
    return true;
}

/**
 * 创建原生摄像头适配器，不支持的平台返回明确的能力错误。
 *
 * @return 独占拥有的原生摄像头适配器；平台不支持时抛出能力错误。
 */
std::unique_ptr<CameraCapture> make_camera_capture() {
    return std::make_unique<MfCamera>();
}
} // namespace xmax::detail
