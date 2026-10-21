//
//  AVFoundationCamera.mm
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "Foundation/Media/Camera/CameraCapture.hpp"
#include <AVFoundation/AVFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <cstring>
#include <mutex>

namespace xmax::detail {

/**
 * 保存原生摄像头回调及其活动状态，通过互斥锁协调停止和帧交付。
 */
struct AppleCaptureState {

    /**
     * 串行协调样本交付、活动标记及停止采集的互斥锁。
     */
    std::mutex mutex;

    /**
     * 当前原生采集状态是否仍允许交付样本。
     */
    bool active = true;

    /**
     * 原生帧与错误交付回调，停止采集时排空并解除。
     */
    CaptureCallbacks callbacks;
};
} // namespace xmax::detail

/**
 * 接收 AVFoundation 样本并交付 SDK 拥有的像素，采集失败时通知媒体控制器。
 */
@interface XmaxCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
  @public

    /**
     * 与采集控制器共享的样本交付状态。
     */
    std::shared_ptr<xmax::detail::AppleCaptureState> state;
}
@end
@implementation XmaxCaptureDelegate

/**
 * 复制原生视频样本并交付拥有像素的 BGRA 帧，失败时停止交付并报告错误。
 *
 * @param[in] output 产生本次视频样本的 AVFoundation 输出对象，仅在回调期间借用。
 * @param[in] sample 当前原生视频样本，借用有效期仅限回调或同步复制期间。
 * @param[in] connection 交付本次样本的 AVFoundation 连接，仅在回调期间借用。
 */
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
                std::memcpy(
                    frame.planes[0].data() + y * frame.strides[0], base + y * stride, frame.strides[0]
                );
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

/**
 * 枚举系统支持的内置与外置摄像头设备。
 *
 * @return 当前自动释放池管理的摄像头设备数组。
 */
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

/**
 * 通过 AVFoundation 枚举并采集摄像头；停止时排空样本回调。
 */
class AppleCamera final : public CameraCapture {

    /**
     * 当前控制器拥有的 AVFoundation 采集会话，停止后释放。
     */
    AVCaptureSession* session_ = nil;

    /**
     * 已安装到采集会话的视频样本输出对象。
     */
    AVCaptureVideoDataOutput* output_ = nil;

    /**
     * 接收原生视频样本的委托对象。
     */
    XmaxCaptureDelegate* delegate_ = nil;

    /**
     * 串行交付视频样本回调的调度队列。
     */
    dispatch_queue_t queue_ = nil;

    /**
     * 与视频样本委托共享的采集状态。
     */
    std::shared_ptr<AppleCaptureState> state_;

  public:

    /**
     * 停止摄像头采集并排空回调，然后释放拥有的媒体资源。
     */
    ~AppleCamera() override {
        stop();
    }

    /**
     * 枚举 AVFoundation 摄像头及系统默认设备。
     *
     * @return 摄像头设备信息快照，不存在可用设备时为空集合。
     */
    std::vector<CameraDevice> devices() override {
        @autoreleasepool {
            std::vector<CameraDevice> result;
            auto default_id = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo].uniqueID;
            for (AVCaptureDevice* device in video_devices())
                result.push_back(
                    {device.uniqueID.UTF8String, device.localizedName.UTF8String,
                     [device.uniqueID isEqualToString:default_id] != NO}
                );

            return result;
        }
    }

    /**
     * 启动 AVFoundation 摄像头采集，并交付 SDK 拥有的 BGRA 帧。
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

            require(
                [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] ==
                    AVAuthorizationStatusAuthorized,
                XMAX_MEDIA_ERROR, "Camera access denied; check the host application's camera permission"
            );

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

    /**
     * 停止 AVFoundation 采集，并在返回前排空样本回调。
     */
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
    return std::make_unique<AppleCamera>();
}
} // namespace xmax::detail
