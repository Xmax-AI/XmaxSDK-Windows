# Windows 使用说明

当前版本 `0.2.0`。公开入口是 [C API](../include/xmax/xmax.h) 和 [C++ RAII 包装](../include/xmax/xmax.hpp)。[XLab 控制器](../Examples/XLab/controller.cpp) 只通过公开 C API 接入，SDK 不依赖 Qt。

C++ 类型分别定义在同名头文件中：`RealtimeModel.hpp`、`RealtimeQualityLevel.hpp`、`RealtimeCameraPreset.hpp`、`XmaxEnvironment.hpp`、`XmaxConfiguration.hpp` 和 `RealtimeConfiguration.hpp`。可以按需单独包含；统一包含 `<xmax/xmax.hpp>` 也会导入这些类型。

客户端、实时会话、存储、帧和日志的 C++ 封装分别位于 `client.hpp`、`realtime.hpp`、`storage.hpp`、`frame.hpp`、`logger.hpp`；这些头文件可独立包含。`xmax.hpp` 只聚合公开头文件，不定义业务类。

## 能力查询与客户端

```cpp
#include <xmax/xmax.hpp>

xmax::client client(xmax::XmaxConfiguration{apiKey, xmax::XmaxEnvironment::china});
auto capabilities = client.capabilities();
auto realtime = client.create_realtime(xmax::RealtimeConfiguration{xmax::RealtimeModel::x2_1});
auto storage = client.create_storage();
```

`XMAX_CAP_RTC` 表示构建包含 RTC 后端；`XMAX_CAP_CAMERA` 表示提供原生采集实现，不保证设备存在或权限已授予；`XMAX_CAP_NATIVE_WINDOW` 表示 Windows HWND 显示实现；`XMAX_CAP_TRAJECTORY` 表示提供坐标映射、任务轨迹信令和效果数据查询。当前 `XMAX_CAP_FRAME_INTERPOLATION` 为零。客户端可以在子对象创建后销毁，Realtime / Storage 仍拥有所需配置和传输对象。

模型由实时配置选择，服务环境由全局配置选择；当前仅支持 `xmax::RealtimeModel::x2_1`，服务端模型标识为 `x2.1`。`x2.1` 支持五组分辨率，模型能力可通过 `xmax::client::model_info(xmax::RealtimeModel::x2_1)` 查询。新入口中，海外通用 API 使用 `api.xmax.cloud`，实时会话使用 `api.xmax.ai`。字符串配置和 C ABI 入口均仅接受 `x2.1`；国内外服务通过环境配置选择。

## SDK 摄像头输入

```cpp
auto devices = client.cameras(); // id、name、is_default；不启动采集
// 在后台线程运行。空 device ID 使用默认摄像头。
realtime.prepare_camera("", {1920, 1080, 30}, false);
```

Windows 使用 Media Foundation；Mac 开发构建使用 AVFoundation。SDK 保留设备实际采集帧的尺寸与像素格式，不对采集帧进行 CPU 缩放、裁剪或补黑边；RTC 根据编码配置处理比例、分辨率和编码。`prepare_camera` 等到第一张真实帧后进入 `ready`，不创建云端 Session。宿主负责其应用的系统权限声明；Mac 应用需要 `NSCameraUsageDescription`，使用麦克风时还需要对应权限声明。

Qt Demo 对齐 macOS XmaxCamera，在启动页右上角提供带摄像头图标、设备名和下拉箭头的胶囊按钮。弹出列表显示当前设备勾选，底部提供 `Refresh Cameras`。多个设备且没有保存的选择时，必须明确选择后才能启动；选择按设备 ID 保存，设备失效时会阻止启动，不会自动改用其他设备。本地视频标签显示当前设备名。修改选择前先停止会话。

在 Parallels 中，宿主 Mac 的虚拟摄像头也可能作为 Windows 摄像头被共享。需要拍摄真人时请选择实际相机（例如 `MacBook Pro的相机`）；`Xmax Virtual Camera` 输出的是虚拟摄像头程序提供的画面。SDK 的空设备 ID 仍沿用默认设备，应用应通过 `cameras()` 枚举并传入选定设备 ID。

摄像头编码预设支持 `1920x1080`、`1080x1920`、`1408x1408`、`1152x1536`、`1536x1152`，帧率为 `1...60 fps`。`x2.1` 模型的摄像头默认使用 `1080x1920@30` 编码目标；设备可能输出其他原生尺寸，本地帧视图报告实际尺寸。`format()` 返回编码目标，不表示相机硬件必定输出该尺寸。旧的 `1920x1024` / `1024x1920` 摄像头配置分别按 `1920x1080` / `1080x1920` 编码，外部输入仍兼容旧尺寸。

生成画布由模型预设中比例最接近的一组尺寸决定，独立于采集尺寸、RTC 上行档位和下行回传尺寸。麦克风由 RTC 后端在连接时启动；摄像头准备阶段只采集视频。准备摄像头后远端音量初始为零。

```cpp
realtime.switch_camera(devices.at(0).id);
realtime.disconnect(); // 保留摄像头采集、本地预览、生成条件
realtime.stop_media(); // 仅允许在 idle / ready 状态；停止本地媒体
```

生成期间切换摄像头会停止旧生成、排空输入、切换采集，并使用缓存条件启动新任务；RTC 房间和服务端 Session 继续复用。切换失败会释放连接和本地媒体，并返回错误。

已就绪、尚未连接的摄像头可以更新规格，保留设备选择、预览绑定及编码配置：

```cpp
realtime.update_camera_format(xmax::RealtimeCameraPreset::resolution1080x1920);
realtime.update_camera_format(xmax_video_format{1408, 1408, 24});
auto format = realtime.camera_format();
auto preset = realtime.camera_preset();
```

更新过程中会产生 `preparing` 状态；准备失败时尝试恢复原规格。没有摄像头流时，查询返回零规格和 `none`。生成期间不能更新媒体基准规格，应先断开连接；上行调档不会改变查询到的摄像头基准规格。

## 宿主外部视频输入

自有采集或解码器可以通过外部帧接口接入，无需使用 SDK 摄像头。准备外部输入会停止之前的本地媒体；连接前才允许重新绑定。

```cpp
realtime.prepare({1920, 1080, 30});

// 由独立的宿主媒体线程持续调用。
xmax_video_frame frame{};
frame.struct_size = sizeof(frame);
frame.pixel_format = XMAX_BGRA; // 也支持 NV12 / I420
frame.width = 1920;
frame.height = 1080;
frame.timestamp_us = monotonicTimestampUs;
frame.planes[0] = pixels;
frame.strides[0] = strideBytes;
frame.plane_sizes[0] = accessibleBytes;
realtime.push(frame);
```

外部输入宽高不必匹配摄像头预设：支持 4 至 8192 的偶数尺寸，总像素不超过 2100000，帧率为 1 至 60。提交帧宽高必须匹配绑定规格；每个平面提供以字节为单位的 stride 和可访问大小。时间戳是每次绑定内严格递增的单调微秒时间。SDK 在 push 返回前复制像素，宿主随后可复用缓冲区。绑定 SDK 摄像头时不允许外部 push。

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

Qt 用 QImage / QVideoFrame 显示 SDK 帧；其他宿主可自行显示 CPU 帧。Windows 还提供 `xmax_frame_render_window(frame, hwnd, XMAX_CONTENT_FIT)`，应由 HWND 所属线程在重绘时调用。支持 FIT / FILL，当前为 GDI CPU 绘制，不提供 D3D11 纹理或自动重绘。WPF 可通过 P/Invoke 获取帧，或通过宿主 HWND 接入。[最小 .NET 消费端](../Examples/DotNetSmoke/Program.cs) 演示 Cdecl、UTF-8、Pack=8 和句柄释放；它用于 ABI 验证，不是完整托管 SDK。

## 镜像

本地预览、远端结果显示和上行发布分别配置，默认均关闭。自拍界面通常启用前两项，并保持发布镜像关闭，使服务端收到原始方向的视频：

```cpp
realtime.video_mirror(true, true, false);
```

C 接口使用 `xmax_video_mirror_config` 和 `xmax_realtime_set_video_mirror`。设置可在准备媒体前或连接后调用，断开重连后继续保留。修改发布镜像时由声网编码器执行，不在 CPU 上逐像素翻转；画质变化也会保留该设置。RTC 更新失败时整组镜像配置保持原值。

显示镜像不改变帧的像素视图。`frame.mirrored()` / `xmax_frame_get_mirrored` 返回取帧时的显示镜像标记，已取得的帧不随后续配置改变。SDK 的 HWND 渲染自动应用此标记；自定义渲染应读取该标记并应用一次水平翻转。例如 Qt 6.8 可设置 `QVideoFrame::setMirrored`，WPF 可在显示变换中处理。不要在宿主界面再次叠加镜像，也不要将显示变换后的画面重新推流。轨迹坐标仍以显示区域为准。

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

编码最小码率 `-1` 使用 `min(500, 最大码率)` kbps、`0` 不设最小限制；最大码率 `0` 使用估算值并保证不低于最小值。偏好支持自动、优先帧率和优先画质。

自适应策略上下行独立，从第 2 档开始，最多 5 档；连续 3 次较差网络降档、5 次优良网络升档，包含稳定期、升档探测和退避。上行调整 RTC 编码，下行发送 `change_target_size` 信令。成功应用的档位按模型和上下行分别写入用户缓存，跨 Realtime 实例和进程复用，1 小时后恢复默认。损坏、未来时间戳或不可读缓存使用默认档位；缓存写入失败不影响实时会话。

状态快照通过 `state()` 获取，不消耗事件。`QUALITY_CHANGE` 的 direction 区分上下行，quality 表示档位；失败时 code 为负数且保留旧档位。`PERFORMANCE` 的 code 为 1 表示设备受限、0 表示恢复。统计和耗时的 `-1` 表示不可获得。

生成中可以分别指定上下行档位。L1 最高，L5 最节省带宽；手动设置不会关闭网络自适应，发送或编码失败时保留原档位。没有活动连接时，`quality()` 返回的档位为 0，表示尚未生效。

```cpp
realtime.uplink_quality_level(xmax::RealtimeQualityLevel::l1);
realtime.downlink_quality_level(xmax::RealtimeQualityLevel::l3);
```

`state_info()` 返回状态、状态原因和关联错误；`poll(xmax_event_info&)` 消费同一事件队列并保留这些详情。连接或生成失败后，回滚状态保留原始错误；新的有效操作清除旧失败原因。可通过 `disconnect(XMAX_REASON_ORIENTATION_CHANGED)` 表达方向变化。

C 接口在失败线程上提供 `xmax_last_error_info`，包含 SDK 错误码、服务端业务码与 HTTP 状态码。它不会清除当前错误；下一次普通 SDK 调用会更新诊断。C++ 异常通过 `code()`、`api_code()`、`http_status()` 读取对应字段。系统明确拒绝设备访问时分别报告 `XMAX_CAMERA_PERMISSION_DENIED` 或 `XMAX_MICROPHONE_PERMISSION_DENIED`。

当前调用 `interpolation(true)` 返回 `XMAX_UNSUPPORTED`，关闭插帧可正常调用。没有使用重复帧模拟插帧。

## 轨迹交互

界面采集鼠标或触控点，提供视口尺寸、当前显示视频的实际尺寸和布局方式。SDK 完成坐标映射、适应模式黑边过滤，并向当前任务发送归一化 `tracks` 信令。此接口不等待网络发送，连续输入仅保留最新待发送采样；断开连接或更换任务后丢弃旧采样。

```cpp
xmax_point points[]{{mouseX, mouseY}};
xmax_interaction_frame sample{};
sample.struct_size = sizeof(sample);
sample.points = points;
sample.point_count = 1;
sample.viewport_width = viewWidth;
sample.viewport_height = viewHeight;
sample.displayed_video_width = displayedFrameWidth;
sample.displayed_video_height = displayedFrameHeight;
sample.content_mode = XMAX_CONTENT_FIT;
realtime.submit_interaction(sample);
```

采样坐标与视口尺寸必须使用同一单位。一次最多提交 256 个点，只在生成结果就绪后接受交互。成功表示 SDK 已接收；异步发送失败通过事件报告，不会仅因单次轨迹失败而断开生成。

`trajectory()` 返回有效期为 600 毫秒的最新归一化点集，Qt、WPF 等界面可自行绘制效果。使用 SDK 原生 HWND 呈现时，可在窗口所属线程调用 `realtime.render_window(frame.get(), hwnd, XMAX_CONTENT_FIT)`，同时绘制视频和默认轨迹效果。界面负责安排重绘，SDK 不接管窗口事件循环。

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

## XmaxLogger 日志与诊断

日志行为参考 macOS XmaxLogger：默认关闭；`XMAX_LOG_BUSINESS` 输出业务日志，`XMAX_LOG_PERFORMANCE` 输出 RTC 指标、网络质量与设备性能告警，`XMAX_LOG_ALL` 同时开启两类。分类包括 Realtime、RTC、Media、API、Storage、Room、Stream、Render、Interaction 和 Permission；每行使用 `[Xmax][分类]` 前缀，记录 debug / info / warning / error 级别。尚未实现的交互功能不会生成虚构日志。

摄像头性能日志每两秒汇总一次实际输出帧率、输入尺寸、帧转换平均/最大耗时，以及同步媒体交付平均/最大耗时；后者包含 RTC 推帧与本地预览投递。结合 RTC 的采集、编码、发送帧率，可以区分帧转换阻塞与编码/发送瓶颈。关闭性能日志时不采样这些耗时，不逐帧输出日志。

```cpp
// 在创建 Client 前开启日志；国内服务选择中文，海外服务可选 XMAX_LOG_ENGLISH。
xmax::logger::configure(XMAX_LOG_ALL, XMAX_LOG_CHINESE);
xmax::client client(apiKey);

// 宿主需要展示或保存日志时，使用单独的定时器/工作线程轮询。
xmax_log_record record{};
while (xmax::logger::poll(record)) {
    // record.level / options / timestamp_us / sequence / category / message
    // message 为已处理的 UTF-8 文本，可交给宿主自己的日志输出。
}
xmax::logger::configure(XMAX_LOG_NONE); // 关闭并清空待取日志。
```

C 接口为 `xmax_logger_configure(options, language)`、`xmax_logger_get_config(&options, &language)` 和 `xmax_logger_poll(&record)`；C 调用者须先填写 `record.struct_size = sizeof(record)`。空队列返回 `XMAX_NO_EVENT`。新接口不改变原有 Client/Event 等结构的布局，ABI 版本仍为 1；新增日志记录结构为 pack 8、4160 字节。

配置是进程级的，作用于所有 Client、Realtime 和 Storage；创建/销毁 Client 不会重置显式配置，最后一次成功的 configure 生效。语言由调用者明确选择；XLab 按当前模型选择中文或英文，并像 macOS 示例一样主动开启全部日志。每次重新配置都会清空队列，防止旧配置的内容继续被读取。

Windows 同时通过 `OutputDebugStringW` 输出到调试器；Mac 开发构建使用 OSLog。宿主无需注册跨 DLL 回调。轮询队列最多保留最新 256 条、每条最多 4095 字节的 UTF-8 文本；未及时读取会丢弃旧记录，可通过 sequence 的间隔发现缺失。多个线程可安全调用，但宿主应指定一个消费者读取进程级队列。日志是尽力而为的，不作为业务状态来源，业务状态仍读取事件与状态接口。没有额外的 SDK 文件日志组件。

API 诊断记录方法、去除查询参数的路径、HTTP 状态、耗时与响应字节数；成功响应不输出正文。失败响应仅处理不超过 64 KiB 的 JSON 对象，提取 code、message、error、reason 等已知错误字段；data、credentials 和未知载荷不输出，嵌套 API Key、token、签名、提示词等敏感字段隐藏，输出最多 2048 字节。非 JSON、数组和标量正文省略。已知 API Key、RTC/COS 凭证也会从异常文本中替换，URL 查询参数不输出。单条日志过长会在 UTF-8 字符边界截断。业务请求、认证头、原始信令分片和帧像素不写入日志。

RTC 诊断包含采集/编码/发送或解码/渲染帧率、码率、丢包率与端到端估计；未提供的指标显示 `—`。业务日志另有会话状态、启动阶段耗时与信令分包数。Agora 原始错误码/说明及 WinHTTP 错误码/系统说明保留到诊断信息，不再只显示通用失败文字。

## C ABI、线程与所有权

- C ABI 使用 `__cdecl`、UTF-8 字符串、固定宽度整数和 opaque handle，结构采用 pack 8。`struct_size` 必须初始化；本次新增入口保留 ABI 版本 1 和原有结构布局。Windows 当前构建目标为 x64，`size_t` 与指针均为 64 位。
- 生命周期、存储和摄像头操作会阻塞，放在工作线程；状态/规格/音量/画质查询也可能等待生命周期队列。`poll_event`、本地/远端 acquire 是非阻塞邮箱操作，存储 progress / cancel 可以与传输并行。
- SDK 不直接调用宿主应用回调。C 函数返回负错误码，调用线程上的 `xmax_last_error()` 提供详情；C++ 包装转换为 `xmax::error`。
- 跨 DLL 资源由对应 SDK release / destroy 释放。摄像头字符串归列表所有；上传信息字符串归 uploaded_file 所有；帧像素归 frame 所有。C++ RAII 对这些对象采用不可复制、可移动的所有权。
- `disconnect` / `close` 可从另一线程取消正在启动的连接或采集。销毁句柄前必须停止并等待所有使用该句柄的宿主线程；销毁不是并发取消操作。

Windows 实机的设备、权限、RTC、上传与生成联调仍需单独验收，Mac 的自动测试使用可控替身覆盖协议和生命周期。

## 源码排版与接口注释

SDK 的 C/C++ 接口文档使用多行 `/** ... */` Doxygen 注释，新增和修订的源码注释统一使用英文，重点说明实际行为、调用约束和资源所有权。Visual Studio 支持此格式的提示与注释补全。格式参考见 [Visual Studio 官方说明](https://devblogs.microsoft.com/cppblog/doxygen-and-xml-doc-comment-support/) 和 [Doxygen 文档](https://www.doxygen.nl/manual/docblocks.html)。

- 方法、函数之间保留一个空行；短方法也展开函数体，不把多个方法挤在一起。根目录 `.clang-format` 的 `AllowShortFunctionsOnASingleLine: None` 和 `SeparateDefinitionBlocks: Always` 固定此规则。
- 每个 SDK 方法声明都写用途说明 `@brief`，包括构造、析构、重载及私有方法。契约以头文件为准；对应实现无需重复整段说明，局部辅助函数和平台回调在定义处注明职责。
- 公共接口逐项说明 `@param[in]` / `@param[out]` / `@param[in,out]`；有返回值时写 `@return`，特殊状态可用 `@retval`。C++ 异常用 `@throws`；C ABI 以错误码和 `xmax_last_error()` 报错，不向外抛异常。
- 涉及阻塞、并发、取消、指针借用、资源所有权和释放时机时，必须写明；补充约定用 `@note`，必须遵守的生命周期限制用 `@warning`。C++ 包装使用 `@see` 关联底层 C 接口。
- 实现内的普通 `//` 注释解释设计原因、边界和平台限制，避免逐行复述代码。接口行为改变时同步更新注释。

```cpp
/// @brief Consume the oldest process-wide log record without waiting.
/// @param[out] record Receives a record; struct_size is initialized automatically.
/// @return True if copied, false if the queue is empty.
/// @see xmax_logger_poll
static bool poll(xmax_log_record& record);

/// @brief Read the process-wide logger options and language.
/// @return A pair containing the option mask and language.
static std::pair<uint32_t, xmax_log_language> configuration();
```
