#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <AVFoundation/AVFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <cstring>
#include <mutex>

namespace xmax::detail {
struct AppleCaptureState {
    std::mutex mutex;
    bool active = true;
    CaptureCallbacks callbacks;
};
} // namespace xmax::detail
@interface XmaxCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
  @public
    std::shared_ptr<xmax::detail::AppleCaptureState> state;
}
@end
@implementation XmaxCaptureDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sample
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)connection;
    using namespace xmax::detail;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->active)
        return;
    try {
        CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
        require(pixels != nullptr, XMAX_MEDIA_ERROR, "Camera delivered no pixel buffer");
        const auto result = CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
        require(result == kCVReturnSuccess, XMAX_MEDIA_ERROR, "Cannot lock camera pixels");
        Frame frame;
        try {
            frame.width = uint32_t(CVPixelBufferGetWidth(pixels));
            frame.height = uint32_t(CVPixelBufferGetHeight(pixels));
            frame.timestamp_us =
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
                    .count();
            frame.strides[0] = size_t(frame.width) * 4;
            frame.planes[0].resize(frame.strides[0] * frame.height);
            const auto* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
            const auto stride = CVPixelBufferGetBytesPerRow(pixels);
            for (uint32_t y = 0; y < frame.height; ++y)
                std::memcpy(frame.planes[0].data() + y * frame.strides[0], base + y * stride,
                            frame.strides[0]);
        } catch (...) {
            CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
            throw;
        }
        CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
        state->callbacks.frame(std::move(frame));
    } catch (const std::exception& error) {
        state->active = false;
        if (state->callbacks.failure) {
            try {
                state->callbacks.failure(Error(XMAX_MEDIA_ERROR, error.what()));
            } catch (...) {
            }
        }
    }
}
@end

namespace xmax::detail {
namespace {
NSArray<AVCaptureDevice*>* video_devices() {
    NSMutableArray<AVCaptureDeviceType>* types =
        [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available(macOS 14.0, *))
        [types addObject:AVCaptureDeviceTypeExternal];
    else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    return
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionUnspecified]
            .devices;
}
class AppleCamera final : public CameraCapture {
    AVCaptureSession* session_ = nil;
    AVCaptureVideoDataOutput* output_ = nil;
    XmaxCaptureDelegate* delegate_ = nil;
    dispatch_queue_t queue_ = nil;
    std::shared_ptr<AppleCaptureState> state_;

  public:
    ~AppleCamera() override { stop(); }
    std::vector<CameraDevice> devices() override {
        @autoreleasepool {
            std::vector<CameraDevice> result;
            auto default_id = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo].uniqueID;
            for (AVCaptureDevice* device in video_devices())
                result.push_back({device.uniqueID.UTF8String, device.localizedName.UTF8String,
                                  [device.uniqueID isEqualToString:default_id] != NO});
            return result;
        }
    }
    void start(const std::string& id, xmax_video_format format, CaptureCallbacks callbacks,
               const Cancel& cancel) override {
        @autoreleasepool {
            stop();
            require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
            const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
            if (status == AVAuthorizationStatusNotDetermined) {
                dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                         completionHandler:^(BOOL) {
                                           dispatch_semaphore_signal(semaphore);
                                         }];
                const auto until = Clock::now() + std::chrono::seconds(30);
                while (dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 20000000)) != 0) {
                    require(!cancel(), XMAX_CANCELLED, "Camera permission request cancelled");
                    require(Clock::now() < until, XMAX_TIMEOUT, "Camera permission request timed out");
                }
            }
            require([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] ==
                        AVAuthorizationStatusAuthorized,
                    XMAX_MEDIA_ERROR, "Camera access denied; check the host application's camera permission");
            AVCaptureDevice* selected =
                id.empty() ? [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo] : nil;
            for (AVCaptureDevice* device in video_devices())
                if (id == device.uniqueID.UTF8String)
                    selected = device;
            require(selected != nil, XMAX_MEDIA_ERROR, "Requested camera is unavailable");
            NSError* error = nil;
            AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:selected error:&error];
            require(input != nil, XMAX_MEDIA_ERROR, "Cannot open the selected camera");
            session_ = [[AVCaptureSession alloc] init];
            [session_ beginConfiguration];
            if ([session_ canSetSessionPreset:AVCaptureSessionPreset1920x1080])
                session_.sessionPreset = AVCaptureSessionPreset1920x1080;
            else if ([session_ canSetSessionPreset:AVCaptureSessionPreset1280x720])
                session_.sessionPreset = AVCaptureSessionPreset1280x720;
            require([session_ canAddInput:input], XMAX_MEDIA_ERROR, "Cannot attach camera input");
            [session_ addInput:input];
            output_ = [[AVCaptureVideoDataOutput alloc] init];
            output_.videoSettings =
                @{(__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
            output_.alwaysDiscardsLateVideoFrames = YES;
            state_ = std::make_shared<AppleCaptureState>();
            state_->callbacks = std::move(callbacks);
            delegate_ = [[XmaxCaptureDelegate alloc] init];
            delegate_->state = state_;
            queue_ = dispatch_queue_create("ai.xmax.sdk.camera", DISPATCH_QUEUE_SERIAL);
            [output_ setSampleBufferDelegate:delegate_ queue:queue_];
            require([session_ canAddOutput:output_], XMAX_MEDIA_ERROR, "Cannot attach camera output");
            [session_ addOutput:output_];
            [session_ commitConfiguration];
            if ([selected lockForConfiguration:&error]) {
                const auto target = CMTimeMake(1, int32_t(format.fps));
                for (AVFrameRateRange* range in selected.activeFormat.videoSupportedFrameRateRanges)
                    if (format.fps >= range.minFrameRate && format.fps <= range.maxFrameRate) {
                        selected.activeVideoMinFrameDuration = target;
                        selected.activeVideoMaxFrameDuration = target;
                        break;
                    }
                [selected unlockForConfiguration];
            }
            require(!cancel(), XMAX_CANCELLED, "Camera startup cancelled");
            [session_ startRunning];
            require(session_.isRunning, XMAX_MEDIA_ERROR, "Camera capture did not start");
        }
    }
    void stop() noexcept override {
        @autoreleasepool {
            if (state_) {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->active = false;
                state_->callbacks = {};
            }
            [output_ setSampleBufferDelegate:nil queue:nil];
            if (session_.isRunning)
                [session_ stopRunning];
            if (queue_)
                dispatch_sync(queue_, ^{
                              });
            session_ = nil;
            output_ = nil;
            delegate_ = nil;
            queue_ = nil;
            state_.reset();
        }
    }
};
} // namespace
bool camera_available() noexcept {
    return true;
}
std::unique_ptr<CameraCapture> make_camera_capture() {
    return std::make_unique<AppleCamera>();
}
} // namespace xmax::detail
