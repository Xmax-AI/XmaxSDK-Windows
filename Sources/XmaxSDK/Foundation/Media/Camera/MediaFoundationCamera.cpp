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
 * Checks an HRESULT, preserving its code and diagnosing camera access denial.
 *
 * @param[in] result The HRESULT to check; failures preserve its value in a media error.
 * @param[in] operation The diagnostic name of the native camera operation.
 */
void checked(
    HRESULT result,
    const char* operation
) {
    if (result == E_ACCESSDENIED)
        XmaxLogger::write(
            LogCategory::Permission,
            XMAX_LOG_ERROR,
            [](auto language) {
                return XmaxLogger::localized(
                    language,
                    "摄像头访问被系统拒绝 (Camera Access Denied)",
                    "Camera Access Denied"
                );
            }
        );
    require(
        SUCCEEDED(result),
        result == E_ACCESSDENIED ? XMAX_CAMERA_PERMISSION_DENIED : XMAX_MEDIA_ERROR,
        std::string(operation) + " failed (HRESULT " + std::to_string(uint32_t(result)) + ")"
    );
}

/**
 * Exclusively owns a COM interface reference; moving transfers it without incrementing the reference count.
 *
 * @tparam T The handle or interface type managed by this owner.
 */
template <class T>
class Com {

    /**
     * The owned COM reference; null means no resource is retained.
     */
    T* value_ = nullptr;

  public:

    /**
     * Creates an empty COM owner without retaining an interface.
     */
    Com() = default;

    /**
     * Releases the exclusively owned COM reference.
     */
    ~Com() {
        if (value_)
            value_->Release();
    }

    /**
     * Disables copying to prevent multiple exclusive owners from releasing the same resource.
     *
     * @param[in] other The copy source. This operation is deleted and cannot be called.
     */
    Com(
        const Com& other
    ) = delete;

    /**
     * Disables copy assignment to prevent duplicate ownership and release.
     *
     * @param[in] other The copy source. This operation is deleted and cannot be called.
     */
    Com& operator=(
        const Com& other
    ) = delete;

    /**
     * Transfers resource ownership and leaves the source empty.
     *
     * @param[in,out] other The reference owner to move from, left empty after transfer.
     */
    Com(
        Com&& other
    ) noexcept
        : value_(std::exchange(
            other.value_,
            nullptr
        )) {
    }

    /**
     * Releases the current COM reference and takes ownership from the source.
     *
     * @param[in,out] other The source owner, left empty after transfer; self-move assignment is supported.
     *
     * @return A reference to this COM owner.
     */
    Com& operator=(
        Com&& other
    ) noexcept {
        if (this != &other) {
            if (value_)
                value_->Release();
            value_ = std::exchange(
                other.value_,
                nullptr
            );
        }
        return *this;
    }

    /**
     * Returns a borrowed COM pointer without incrementing its reference count.
     *
     * @return The borrowed COM pointer, or nullptr if no interface is held.
     */
    T* get() const {
        return value_;
    }

    /**
     * Provides borrowed COM member access without transferring ownership.
     *
     * @return The borrowed COM pointer without a reference-count increment.
     */
    T* operator->() const {
        return value_;
    }

    /**
     * Provides an empty slot for receiving an owned COM reference.
     *
     * @return The output slot for an owned COM reference; this owner must be empty before use.
     */
    T** put() {
        return &value_;
    }
};

/**
 * Pairs COM and Media Foundation initialization and shutdown on the current thread.
 */
struct Platform {

    /**
     * Whether destruction must call CoUninitialize on the current thread.
     */
    bool uninitialize = false;

    /**
     * Whether Media Foundation startup succeeded and requires shutdown.
     */
    bool started = false;

    /**
     * Initializes COM and Media Foundation on this thread, rolling back completed steps on partial failure.
     */
    Platform() {
        const auto result = CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );
        require(
            SUCCEEDED(result) || result == RPC_E_CHANGED_MODE,
            XMAX_MEDIA_ERROR,
            "Cannot initialize COM"
        );
        uninitialize = SUCCEEDED(result);

        const auto startup = MFStartup(
            MF_VERSION,
            MFSTARTUP_FULL
        );

        if (FAILED(startup)) {
            if (uninitialize)
                CoUninitialize();
            checked(
                startup,
                "MFStartup"
            );
        }

        started = true;
    }

    /**
     * Shuts down Media Foundation and balances COM initialization on the initializing thread.
     */
    ~Platform() {
        if (started)
            MFShutdown();
        if (uninitialize)
            CoUninitialize();
    }
};

/**
 * Converts Windows UTF-16 text with an explicit length to UTF-8.
 *
 * @param[in] value The read-only Windows string containing length UTF-16 code units.
 * @param[in] length The UTF-16 code-unit count, excluding any terminator not explicitly included.
 */
std::string utf8(
    const wchar_t* value,
    UINT32 length
) {
    const int count = WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        int(length),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    std::string text(
        size_t(count),
        '\0'
    );
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        int(length),
        text.data(),
        count,
        nullptr,
        nullptr
    );

    return text;
}

/**
 * Reads a Media Foundation string attribute and releases its native allocation.
 *
 * @param[in] value The Media Foundation attribute object, valid during the call.
 * @param[in] key The GUID of the string attribute to read.
 */
std::string attribute(
    IMFAttributes* value,
    REFGUID key
) {
    wchar_t* text = nullptr;
    UINT32 count = 0;
    checked(
        value->GetAllocatedString(
            key,
            &text,
            &count
        ),
        "Read camera identity"
    );

    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(
        text,
        CoTaskMemFree
    );

    return utf8(
        text,
        count
    );
}

/**
 * Owns the Media Foundation camera activation array and releases its storage and interface references.
 */
struct Devices {

    /**
     * The system-allocated activation array; this object releases the array and its COM references.
     */
    IMFActivate** values = nullptr;

    /**
     * The number of system-allocated activation objects.
     */
    UINT32 count = 0;

    /**
     * Enumerates and takes ownership of camera activation objects and their array.
     */
    Devices() {
        Com<IMFAttributes> attributes;
        checked(
            MFCreateAttributes(
                attributes.put(),
                1
            ),
            "Camera enumeration attributes"
        );
        checked(
            attributes
                ->SetGUID(
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
            ),
            "Camera source type"
        );
        checked(
            MFEnumDeviceSources(
                attributes.get(),
                &values,
                &count
            ),
            "Enumerate camera devices"
        );
    }

    /**
     * Releases the camera activation references and system-allocated array.
     */
    ~Devices() {
        for (UINT32 i = 0; i < count; ++i)
            values[i]->Release();
        CoTaskMemFree(values);
    }
};

/**
 * Retains the asynchronous sample reader, negotiated pixel layout, and capture callbacks.
 */
struct CaptureState {

    /**
     * The mutex protecting the reader, media type, and stop state while serializing sample delivery.
     */
    std::mutex mutex;

    /**
     * The asynchronous reader owned by the current capture session.
     */
    Com<IMFSourceReader> reader;

    /**
     * The current capture session's frame and failure callbacks.
     */
    CaptureCallbacks callbacks;

    /**
     * Whether capture has stopped or failed, preventing further sample requests and delivery.
     */
    bool stopping = false;

    /**
     * The video width, in pixels.
     */
    UINT32 width = 0;

    /**
     * The video height, in pixels.
     */
    UINT32 height = 0;

    /**
     * The negotiated RGB row stride, in bytes; negative indicates reverse row layout.
     */
    LONG stride = 0;

    /**
     * Reads and validates negotiated dimensions and RGB byte stride.
     */
    void type() {
        Com<IMFMediaType> output;
        checked(
            reader->GetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                output.put()
            ),
            "Read camera output type"
        );
        checked(
            MFGetAttributeSize(
                output.get(),
                MF_MT_FRAME_SIZE,
                &width,
                &height
            ),
            "Read camera dimensions"
        );
        require(
            width >= 2 && height >= 2 && uint64_t(width) * height <= 2100000,
            XMAX_MEDIA_ERROR,
            "Camera output dimensions exceed the frame budget"
        );

        UINT32 raw = 0;

        if (SUCCEEDED(output->GetUINT32(MF_MT_DEFAULT_STRIDE, &raw)))
            stride = LONG(raw);
        else
            checked(
                MFGetStrideForBitmapInfoHeader(
                    MFVideoFormat_RGB32.Data1,
                    width,
                    &stride
                ),
                "Read camera stride"
            );
    }

    /**
     * Copies BGRA pixels using the native sample's row stride into an independently owned frame.
     *
     * @param[in] sample The borrowed native video sample, valid only during the callback or synchronous
     * copy.
     */
    Frame copy(
        IMFSample* sample
    ) {
        Com<IMFMediaBuffer> buffer;
        checked(
            sample->ConvertToContiguousBuffer(buffer.put()),
            "Read camera sample"
        );

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
            checked(
                plane->Lock2D(
                    &pixels,
                    &pitch
                ),
                "Lock camera image"
            );

            if (size_t(pitch < 0 ? -int64_t(pitch) : pitch) < frame.strides[0]) {
                plane->Unlock2D();
                throw Error(
                    XMAX_MEDIA_ERROR,
                    "Camera row stride is shorter than its width"
                );
            }

            for (UINT32 y = 0; y < height; ++y)
                std::memcpy(
                    frame.planes[0].data() + y * frame.strides[0],
                    pixels + ptrdiff_t(y) * pitch,
                    frame.strides[0]
                );

            plane->Unlock2D();
        } else {
            DWORD size = 0;
            checked(
                buffer->Lock(
                    &pixels,
                    nullptr,
                    &size
                ),
                "Lock camera buffer"
            );

            const size_t row = size_t(pitch < 0 ? -int64_t(pitch) : pitch);

            if (row < frame.strides[0] || uint64_t(row) * (height - 1) + frame.strides[0] > size) {
                buffer->Unlock();
                throw Error(
                    XMAX_MEDIA_ERROR,
                    "Camera buffer is shorter than its layout"
                );
            }

            if (pitch < 0)
                pixels += row * (height - 1);

            for (UINT32 y = 0; y < height; ++y)
                std::memcpy(
                    frame.planes[0].data() + y * frame.strides[0],
                    pixels + ptrdiff_t(y) * pitch,
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
 * Receives asynchronous Media Foundation reader callbacks through a weak capture-state reference.
 */
class ReaderCallback final : public IMFSourceReaderCallback {

    /**
     * The callback object's atomic COM reference count.
     */
    std::atomic<ULONG> references_{1};

    /**
     * The weak capture-state reference preventing a reader/callback ownership cycle.
     */
    std::weak_ptr<CaptureState> state_;

  public:

    /**
     * Retains a weak capture-state reference to avoid a reader/callback ownership cycle.
     */
    explicit ReaderCallback(
        std::shared_ptr<CaptureState> state
    )
        : state_(state) {
    }

    /**
     * Queries the supported IUnknown or sample-reader callback interface.
     *
     * @param[in] iid The requested COM interface identifier.
     * @param[out] out Receives a retained COM interface pointer; cleared on query failure.
     *
     * @return S_OK on success, E_POINTER for a null output, or E_NOINTERFACE for unsupported interfaces.
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
     * Atomically increments this COM callback's reference count.
     *
     * @return The incremented COM reference count.
     */
    STDMETHODIMP_(
        ULONG
    )

    AddRef() override {
        return ++references_;
    }

    /**
     * Releases one callback reference and destroys the object when the count reaches zero.
     *
     * @return The decremented reference count; 0 means the object was destroyed.
     */
    STDMETHODIMP_(
        ULONG
    )

    Release() override {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }

    /**
     * Processes reader results, media-type changes, and failures, then requests the next sample.
     *
     * @param[in] status The asynchronous read HRESULT; failure stops capture and reports a media error.
     * @param[in] stream_index The media stream index that triggered the callback.
     * @param[in] flags The Media Foundation sample-reader status flags.
     * @param[in] timestamp The native sample timestamp in 100-nanosecond units; XmaxSDK orders frames using
     * its own monotonic clock.
     * @param[in] sample The borrowed native video sample, valid only during the callback or synchronous
     * copy.
     *
     * @return S_OK; read failures are reported through the capture failure callback.
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
            checked(
                status,
                "Camera capture"
            );
            require(
                !(flags & MF_SOURCE_READERF_ENDOFSTREAM),
                XMAX_MEDIA_ERROR,
                "Camera stream ended"
            );

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                state->type();

            if (sample)
                state->callbacks.frame(state->copy(sample));

            checked(
                state->reader
                    ->ReadSample(
                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                    0,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr
                ),
                "Request camera sample"
            );
        } catch (const std::exception& error) {
            state->stopping = true;
            if (state->callbacks.failure) {
                try {
                    state->callbacks.failure(Error(
                        XMAX_MEDIA_ERROR,
                        error.what()
                    ));
                } catch (...) {
                }
            }
        }

        return S_OK;
    }

    /**
     * Acknowledges reader events that require no additional processing.
     *
     * @param[in] stream_index The media stream index that triggered the callback.
     * @param[in] media_event The native media event, borrowed during the callback.
     *
     * @return S_OK to acknowledge the event.
     */
    STDMETHODIMP OnEvent(
        [[maybe_unused]] DWORD stream_index,
        [[maybe_unused]] IMFMediaEvent* media_event
    ) override {
        return S_OK;
    }

    /**
     * Acknowledges reader flushing during capture shutdown.
     *
     * @param[in] stream_index The media stream index that triggered the callback.
     *
     * @return S_OK to acknowledge the reader flush.
     */
    STDMETHODIMP OnFlush(
        [[maybe_unused]] DWORD stream_index
    ) override {
        return S_OK;
    }
};

/**
 * Enumerates and captures Media Foundation cameras, delivering continuous BGRA frames.
 */
class MfCamera final : public CameraCapture {

    /**
     * The COM and Media Foundation initialization retained for the capture lifecycle.
     */
    std::unique_ptr<Platform> platform_;

    /**
     * The owned native camera media source.
     */
    Com<IMFMediaSource> source_;

    /**
     * The capture state shared with reader callbacks.
     */
    std::shared_ptr<CaptureState> state_;

  public:

    /**
     * Stops camera capture, drains callbacks, and releases owned media resources.
     */
    ~MfCamera() override {
        stop();
    }

    /**
     * Enumerates symbolic identifiers and display names of Media Foundation cameras.
     *
     * @return A camera information snapshot, or an empty collection if no cameras are available.
     */
    std::vector<CameraDevice> devices() override {
        Platform platform;
        Devices found;
        std::vector<CameraDevice> result;

        for (UINT32 i = 0; i < found.count; ++i)
            result.push_back(
                {attribute(
                    found.values[i],
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
                ),
                 attribute(
                     found.values[i],
                     MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME
                 ),
                 i == 0}
            );

        return result;
    }

    /**
     * Opens the selected camera, negotiates BGRA output, and starts asynchronous reading.
     *
     * @param[in] id The UTF-8 camera identifier; an empty string selects the default camera.
     * @param[in] format The requested pixel dimensions and frame rate, subject to the interface's format
     * limits.
     * @param[in] callbacks Capture frame and failure callbacks, delivered on the native media thread and
     * drained before stop returns.
     * @param[in] cancel A cooperative cancellation predicate; true requests termination of the operation.
     */
    void start(
        const std::string& id,
        xmax_video_format format,
        CaptureCallbacks callbacks,
        const Cancel& cancel
    ) override {
        stop();
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Camera startup cancelled"
        );
        platform_ = std::make_unique<Platform>();

        Devices found;
        IMFActivate* selected = nullptr;

        for (UINT32 i = 0; i < found.count; ++i)
            if ((id.empty() && i == 0) ||
                attribute(
                    found.values[i],
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
                ) == id) {
                selected = found.values[i];
                break;
            }

        require(
            selected != nullptr,
            XMAX_MEDIA_ERROR,
            "Requested camera is unavailable"
        );

        const auto name = attribute(
            selected,
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME
        );
        XmaxLogger::write(
            LogCategory::Media,
            XMAX_LOG_INFO,
            [&](auto language) {
                return XmaxLogger::localized(
                    language,
                    "选择摄像头 (Selected Camera)",
                    "Selected Camera"
                ) +
                       "\n└─ " + name;
            }
        );
        checked(
            selected->ActivateObject(
                IID_IMFMediaSource,
                reinterpret_cast<void**>(source_.put())
            ),
            "Open camera; check Windows camera permissions"
        );
        state_ = std::make_shared<CaptureState>();
        state_->callbacks = std::move(callbacks);

        Com<IMFAttributes> attributes;
        checked(
            MFCreateAttributes(
                attributes.put(),
                2
            ),
            "Capture attributes"
        );

        auto callback = new ReaderCallback(state_);
        const auto result = attributes->SetUnknown(
            MF_SOURCE_READER_ASYNC_CALLBACK,
            callback
        );
        callback->Release();
        checked(
            result,
            "Capture callback"
        );
        checked(
            attributes->SetUINT32(
                MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,
                TRUE
            ),
            "Camera RGB conversion"
        );
        checked(
            MFCreateSourceReaderFromMediaSource(
                source_.get(),
                attributes.get(),
                state_->reader.put()
            ),
            "Create camera reader"
        );
        state_->reader->SetStreamSelection(
            MF_SOURCE_READER_ALL_STREAMS,
            FALSE
        );
        checked(
            state_->reader->SetStreamSelection(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                TRUE
            ),
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

            MFGetAttributeRatio(
                type.get(),
                MF_MT_FRAME_RATE,
                &numerator,
                &denominator
            );

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

        require(
            best.get() != nullptr,
            XMAX_MEDIA_ERROR,
            "Camera has no supported capture format"
        );
        checked(
            state_->reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr,
                best.get()
            ),
            "Select camera capture format"
        );

        Com<IMFMediaType> output;
        checked(
            MFCreateMediaType(output.put()),
            "Create RGB camera format"
        );
        output->SetGUID(
            MF_MT_MAJOR_TYPE,
            MFMediaType_Video
        );
        output->SetGUID(
            MF_MT_SUBTYPE,
            MFVideoFormat_RGB32
        );
        checked(
            state_->reader->SetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                nullptr,
                output.get()
            ),
            "Negotiate BGRA camera output"
        );
        state_->type();
        require(
            !cancel(),
            XMAX_CANCELLED,
            "Camera startup cancelled"
        );
        checked(
            state_->reader
                ->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            ),
            "Start camera capture"
        );
    }

    /**
     * Stops sample delivery, clears callbacks, and releases native camera resources.
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
 * Reports whether this SDK build provides native camera capture.
 */
bool camera_available() noexcept {
    return true;
}

/**
 * Creates a native camera adapter or reports unsupported capability on other platforms.
 *
 * @return An exclusively owned camera adapter; unsupported platforms throw a capability error.
 */
std::unique_ptr<CameraCapture> make_camera_capture() {
    return std::make_unique<MfCamera>();
}
} // namespace xmax::detail
