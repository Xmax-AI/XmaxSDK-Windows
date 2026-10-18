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
void checked(HRESULT result, const char* operation) {
    require(SUCCEEDED(result), XMAX_MEDIA_ERROR,
            std::string(operation) + " failed (HRESULT " + std::to_string(uint32_t(result)) + ")");
}
template <class T> class Com {
    T* value_ = nullptr;

  public:
    Com() = default;
    ~Com() {
        if (value_)
            value_->Release();
    }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Com& operator=(Com&& other) noexcept {
        if (this != &other) {
            if (value_)
                value_->Release();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    T** put() { return &value_; }
};
struct Platform {
    bool uninitialize = false, started = false;
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
    ~Platform() {
        if (started)
            MFShutdown();
        if (uninitialize)
            CoUninitialize();
    }
};
std::string utf8(const wchar_t* value, UINT32 length) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, int(length), nullptr, 0, nullptr, nullptr);
    std::string text(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, int(length), text.data(), count, nullptr, nullptr);
    return text;
}
std::string attribute(IMFAttributes* value, REFGUID key) {
    wchar_t* text = nullptr;
    UINT32 count = 0;
    checked(value->GetAllocatedString(key, &text, &count), "Read camera identity");
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(text, CoTaskMemFree);
    return utf8(text, count);
}
struct Devices {
    IMFActivate** values = nullptr;
    UINT32 count = 0;
    Devices() {
        Com<IMFAttributes> attributes;
        checked(MFCreateAttributes(attributes.put(), 1), "Camera enumeration attributes");
        checked(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                "Camera source type");
        checked(MFEnumDeviceSources(attributes.get(), &values, &count), "Enumerate camera devices");
    }
    ~Devices() {
        for (UINT32 i = 0; i < count; ++i)
            values[i]->Release();
        CoTaskMemFree(values);
    }
};
struct CaptureState {
    std::mutex mutex;
    Com<IMFSourceReader> reader;
    CaptureCallbacks callbacks;
    bool stopping = false;
    UINT32 width = 0, height = 0;
    LONG stride = 0;
    void type() {
        Com<IMFMediaType> output;
        checked(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, output.put()),
                "Read camera output type");
        checked(MFGetAttributeSize(output.get(), MF_MT_FRAME_SIZE, &width, &height),
                "Read camera dimensions");
        require(width >= 2 && height >= 2 && uint64_t(width) * height <= 2100000, XMAX_MEDIA_ERROR,
                "Camera output dimensions exceed the frame budget");
        UINT32 raw = 0;
        if (SUCCEEDED(output->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw)))
            stride = LONG(raw);
        else
            checked(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride),
                    "Read camera stride");
    }
    Frame copy(IMFSample* sample) {
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
                std::memcpy(frame.planes[0].data() + y * frame.strides[0], pixels + ptrdiff_t(y) * pitch,
                            frame.strides[0]);
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
                std::memcpy(frame.planes[0].data() + y * frame.strides[0], pixels + ptrdiff_t(y) * pitch,
                            frame.strides[0]);
            buffer->Unlock();
        }
        for (size_t i = 3; i < frame.planes[0].size(); i += 4)
            frame.planes[0][i] = 255;
        return frame;
    }
};
class ReaderCallback final : public IMFSourceReaderCallback {
    std::atomic<ULONG> references_{1};
    std::weak_ptr<CaptureState> state_;

  public:
    explicit ReaderCallback(std::shared_ptr<CaptureState> state) : state_(state) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
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
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
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
            checked(state->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr,
                                              nullptr, nullptr),
                    "Request camera sample");
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
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
};
class MfCamera final : public CameraCapture {
    std::unique_ptr<Platform> platform_;
    Com<IMFMediaSource> source_;
    std::shared_ptr<CaptureState> state_;

  public:
    ~MfCamera() override { stop(); }
    std::vector<CameraDevice> devices() override {
        Platform platform;
        Devices found;
        std::vector<CameraDevice> result;
        for (UINT32 i = 0; i < found.count; ++i)
            result.push_back(
                {attribute(found.values[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                 attribute(found.values[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME), i == 0});
        return result;
    }
    void start(const std::string& id, xmax_video_format format, CaptureCallbacks callbacks,
               const Cancel& cancel) override {
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
        checked(selected->ActivateObject(IID_IMFMediaSource, reinterpret_cast<void**>(source_.put())),
                "Open camera; check Windows camera permissions");
        state_ = std::make_shared<CaptureState>();
        state_->callbacks = std::move(callbacks);
        Com<IMFAttributes> attributes;
        checked(MFCreateAttributes(attributes.put(), 2), "Capture attributes");
        auto callback = new ReaderCallback(state_);
        const auto result = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback);
        callback->Release();
        checked(result, "Capture callback");
        checked(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
                "Camera RGB conversion");
        checked(MFCreateSourceReaderFromMediaSource(source_.get(), attributes.get(), state_->reader.put()),
                "Create camera reader");
        state_->reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        checked(state_->reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE),
                "Select camera video");
        Com<IMFMediaType> best;
        uint64_t best_score = std::numeric_limits<uint64_t>::max();
        for (DWORD i = 0;; ++i) {
            Com<IMFMediaType> type;
            if (FAILED(
                    state_->reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, type.put())))
                break;
            UINT32 w = 0, h = 0, numerator = 0, denominator = 1;
            if (FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h)) || w < 2 || h < 2 || w % 2 ||
                h % 2 || uint64_t(w) * h > 2100000)
                continue;
            MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &numerator, &denominator);
            const double fps = denominator ? double(numerator) / denominator : 0;
            const auto score =
                uint64_t(std::abs(int64_t(w) - format.width) + std::abs(int64_t(h) - format.height)) +
                uint64_t(std::abs(fps - format.fps) * 100);
            if (score < best_score) {
                best_score = score;
                best = std::move(type);
            }
        }
        require(best.get() != nullptr, XMAX_MEDIA_ERROR, "Camera has no supported capture format");
        checked(state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, best.get()),
                "Select camera capture format");
        Com<IMFMediaType> output;
        checked(MFCreateMediaType(output.put()), "Create RGB camera format");
        output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        checked(
            state_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.get()),
            "Negotiate BGRA camera output");
        state_->type();
        require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
        checked(state_->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr,
                                           nullptr),
                "Start camera capture");
    }
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
bool camera_available() noexcept {
    return true;
}
std::unique_ptr<CameraCapture> make_camera_capture() {
    return std::make_unique<MfCamera>();
}
} // namespace xmax::detail
