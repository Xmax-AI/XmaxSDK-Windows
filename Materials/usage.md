# Windows 使用说明

当前版本 `0.2.0`。公开入口是 [C API](../include/xmax/xmax.h) 和 [C++ RAII 包装](../include/xmax/xmax.hpp)。[XLab 控制器](../Examples/XLab/controller.cpp) 只通过公开 C API 接入，SDK 不依赖 Qt。

## 能力查询与客户端

```cpp
#include <xmax/xmax.hpp>

xmax::client client(apiKey); // 默认 x2.1.1-preview-cn；也支持 x2.1.1-preview-us
auto capabilities = client.capabilities();
auto realtime = client.create_realtime();
auto storage = client.create_storage();
```

`XMAX_CAP_RTC` 表示构建包含 RTC 后端；`XMAX_CAP_CAMERA` 表示提供原生采集实现，不保证设备存在或权限已授予；`XMAX_CAP_NATIVE_WINDOW` 表示 Windows HWND 显示实现。当前 `XMAX_CAP_FRAME_INTERPOLATION` 为零。客户端可以在子对象创建后销毁，Realtime / Storage 仍拥有所需配置和传输对象。

## SDK 摄像头输入

```cpp
auto devices = client.cameras(); // id、name、is_default；不启动采集
// 在后台线程运行。空 device ID 使用默认摄像头。
realtime.prepare_camera("", {1920, 1024, 30}, false);
```

Windows 使用 Media Foundation；Mac 开发构建使用 AVFoundation。采集与 CPU 帧转换、等比例缩放和黑边填充由 SDK 完成。`prepare_camera` 等到第一张真实帧后进入 `ready`，不创建云端 Session。宿主负责其应用的系统权限声明；Mac 应用需要 `NSCameraUsageDescription`，使用麦克风时还需要对应权限声明。

模型输入支持 `1920x1024` 或 `1024x1920`、`1...60 fps`。麦克风由 RTC 后端在连接时启动；摄像头准备阶段只采集视频。准备摄像头后远端音量初始为零。

```cpp
realtime.switch_camera(devices.at(0).id);
realtime.disconnect(); // 保留摄像头采集、本地预览、生成条件
realtime.stop_media(); // 仅允许在 idle / ready 状态；停止本地媒体
```

生成期间切换摄像头会停止旧生成、排空输入、切换采集，并使用缓存条件启动新任务；RTC 房间和服务端 Session 继续复用。切换失败会释放连接和本地媒体，并返回错误。

## 宿主外部视频输入

自有采集或解码器可以通过外部帧接口接入，无需使用 SDK 摄像头。准备外部输入会停止之前的本地媒体；连接前才允许重新绑定。

```cpp
realtime.prepare({1920, 1024, 30});

// 由独立的宿主媒体线程持续调用。
xmax_video_frame frame{};
frame.struct_size = sizeof(frame);
frame.pixel_format = XMAX_BGRA; // 也支持 NV12 / I420
frame.width = 1920;
frame.height = 1024;
frame.timestamp_us = monotonicTimestampUs;
frame.planes[0] = pixels;
frame.strides[0] = strideBytes;
frame.plane_sizes[0] = accessibleBytes;
realtime.push(frame);
```

宽高必须匹配绑定规格；每个平面提供以字节为单位的 stride 和可访问大小。时间戳是每次绑定内严格递增的单调微秒时间。SDK 在 push 返回前复制像素，宿主随后可复用缓冲区。绑定 SDK 摄像头时不允许外部 push。

`requesting_frames()` 表示 RTC 是否需要外部输入，摄像头模式始终返回 false。未连接时也可以提交有效外部帧以获得 SDK 本地预览。开始生成阻塞等待结果首帧，须由另一线程持续供帧；需求标志不承担宿主帧率控制。

## 连接、生成与条件复用

```cpp
realtime.connect(); // 只连接服务端和 RTC，不发送开始生成信令
realtime.generate("视频中角色替换成参考图中角色", referenceURL);
realtime.update("视频风格变为参考图指定的风格", anotherReferenceURL);

// 或在 ready 状态一次完成连接和生成：
// realtime.start(prompt, referenceURL, useMicrophone, faceRestoration);
```

`generate` 在 ready 状态自动连接，在 connected 状态开始新任务，在 generating 状态更新现有任务。`start` 保留相同的组合入口。参考图使用上传后的 URL；更新时传入空参考路径会省略该条件。虚拟试衣将 `faceRestoration` 设为 true，对应服务端 `face_replace=true`。

```cpp
realtime.disconnect();
realtime.generate(); // 重新连接并复用上次成功的提示词、参考图和人脸选项
realtime.close();    // 关闭连接、停止本地媒体并清除缓存条件
```

首次生成必须提供条件。关闭后重新准备媒体，再传入完整条件。空 API Key 可用于离线本地设置，连接时会返回错误。缺少 RTC 后端时返回 `XMAX_UNSUPPORTED`，不会先分配服务端 Session。

## 本地预览与生成结果

```cpp
auto local = realtime.acquire_local_frame();
auto remote = realtime.acquire_frame();
if (remote) {
    auto view = remote.view();
    // 同步复制或消费 view；指针仅在 remote 帧句柄有效期间可用。
}
```

两条 Render 管线都保留最新未消费帧，旧连接的回调不会覆盖新连接。无新帧时 C API 返回 `XMAX_NO_EVENT`，C++ 包装返回空 frame。已获取的帧独立持有像素，Realtime 关闭或销毁后仍可读取。

Qt 用 QImage / QVideoFrame 显示 SDK 帧；其他宿主可自行显示 CPU 帧。Windows 还提供 `xmax_frame_render_window(frame, hwnd, XMAX_CONTENT_FIT)`，应由 HWND 所属线程在重绘时调用。支持 FIT / FILL，当前为 GDI CPU 绘制，不提供 D3D11 纹理或自动重绘。WPF 可通过 P/Invoke 获取帧，或通过宿主 HWND 接入；当前没有随 SDK 交付 C# 包装。

## 编码、画质与事件

```cpp
realtime.encoding(-1, 0, XMAX_ENCODER_MAINTAIN_FRAMERATE);
realtime.adaptive_quality(true);
auto quality = realtime.quality();
realtime.volume(0.5f);

xmax_event event{};
while (realtime.poll(event)) {
    // STATE / ERROR / STATISTICS / TIMING / QUALITY_CHANGE / PERFORMANCE
}
```

编码最小码率 `-1` 使用策略值、`0` 不设最小限制；最大码率 `0` 使用估算值并保证不低于最小值。偏好支持自动、优先帧率和优先画质。

自适应策略上下行独立，从第 2 档开始，最多 5 档；连续 3 次较差网络降档、5 次优良网络升档，包含稳定期、升档探测和退避。上行调整 RTC 编码，下行发送 `change_target_size` 信令。档位保存在当前 Realtime 实例内，1 小时后重连恢复默认；尚未持久化到磁盘。

状态快照通过 `state()` 获取，不消耗事件。`QUALITY_CHANGE` 的 direction 区分上下行，quality 表示档位；失败时 code 为负数且保留旧档位。`PERFORMANCE` 的 code 为 1 表示设备受限、0 表示恢复。统计和耗时的 `-1` 表示不可获得。

当前调用 `interpolation(true)` 返回 `XMAX_UNSUPPORTED`，关闭插帧可正常调用。没有使用重复帧模拟插帧。

## 图片、视频上传与下载

```cpp
// 后台线程运行；第二个参数表示进行图片安全检查。
auto image = storage.upload_file(localImagePath, true);
auto referenceURL = image.info().url;

// MIME 类型可由文件名推断，也可以显式传入。
auto video = storage.upload_file(localVideoPath, false, "video/mp4");
auto bytes = storage.download(remoteURL, newDestinationPath);
```

SDK 完成 STS 获取、COS HMAC 签名、HTTPS PUT、可选图片安全检查，并返回 URL / object key / ETag。图片最多 20 MiB，视频最多 256 MiB；当前上传将文件读入内存，尚未实现分片续传。上传数据接口接受编码后的文件字节，而非视频帧像素。

下载只接受 HTTPS，不跟随重定向；边下载边写入临时文件，成功后发布到目标路径。目标已存在时失败，取消或失败时删除临时文件。下载上限为 256 MiB。

```cpp
// 其他线程可在传输时查询或取消。
auto progress = storage.progress(); // state / transferred / total
storage.cancel();
```

每个 Storage 句柄同时只允许一项传输，多项并发使用独立句柄。进度的 total 为 0 时表示总量未知。取消只影响当前操作，句柄可继续复用。Windows 取消在网络检查点生效，受 WinHTTP 当前阶段超时约束。`xmax_client_request_json` 继续用于其他相对 API 路径，不再需要宿主实现 COS 协议。

## C ABI、线程与所有权

- C ABI 使用 `__cdecl`、UTF-8 字符串、固定宽度整数和 opaque handle，结构采用 pack 8。`struct_size` 必须初始化；本次新增入口保留 ABI 版本 1 和原有结构布局。Windows 当前构建目标为 x64，`size_t` 与指针均为 64 位。
- 生命周期、存储和摄像头操作会阻塞，放在工作线程；状态/规格/音量/画质查询也可能等待生命周期队列。`poll_event`、本地/远端 acquire 是非阻塞邮箱操作，存储 progress / cancel 可以与传输并行。
- SDK 不直接调用宿主应用回调。C 函数返回负错误码，调用线程上的 `xmax_last_error()` 提供详情；C++ 包装转换为 `xmax::error`。
- 跨 DLL 资源由对应 SDK release / destroy 释放。摄像头字符串归列表所有；上传信息字符串归 uploaded_file 所有；帧像素归 frame 所有。C++ RAII 对这些对象采用不可复制、可移动的所有权。
- `disconnect` / `close` 可从另一线程取消正在启动的连接或采集。销毁句柄前必须停止并等待所有使用该句柄的宿主线程；销毁不是并发取消操作。

Windows 实机的设备、权限、RTC、上传与生成联调仍需单独验收，Mac 的自动测试使用可控替身覆盖协议和生命周期。
