# C# SDK

`Sources/XmaxSDK.Net` 提供通用 C# 封装，程序集和命名空间为 `Xmax.SDK`。依赖 .NET 10；Windows 原生 SDK 仅支持 x64 进程。该类库不依赖 WPF、Qt 或 C++/CLI。

界面适配分为 `Sources/XmaxSDK.Wpf`（`Xmax.SDK.Wpf`）与 `Sources/XmaxSDK.WinUI`（`Xmax.SDK.WinUI`）。两者使用同一个视频源和后台最新帧队列，分别通过 WPF / WinUI 3 的位图和渲染节拍显示视频。示例工作流统一放在 `Examples/XLabShared`，通过调度器、计时器和缩略图接口接入两套 UI。

解决方案入口是根目录 `XmaxSDK.DotNet.sln`，可以使用 .NET CLI 或支持 .NET 10 的 Visual Studio 2026；现有原生 SDK 仍可使用 Visual Studio 2022 构建。

## 构建与测试

先按[安装说明](installation.md)构建原生 SDK，再从仓库根目录执行：

```powershell
dotnet build XmaxSDK.DotNet.sln -c Release -p:Platform=x64 `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/
```

原生 C ABI 布局探针由 CMake 的 `xmax_dotnet_abi_probe` 目标生成。将其输出传给托管集成测试：

```powershell
& C:/path/to/native-build/Release/xmax_dotnet_abi_probe.exe | Set-Content C:/dev/native-layout.txt -Encoding UTF8
dotnet run --project Tests/XmaxSDKNetTests -c Release `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/ -- C:/dev/native-layout.txt
dotnet run --project Tests/XmaxSDKWpfTests -c Release `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/
```

这些测试比较公共结构体布局与事件值，检查真实 DLL 的所有权、取消、帧交付和 WPF 显示，不打开摄像头或创建云端生成会话。界面与真实设备验收通过各 Demo 的显式冒烟模式执行；运行真实云端模式前，通过安全的本机方式提供 `XMAX_API_KEY`。

## 使用

```csharp
using Xmax.SDK;

// 开发阶段可以显式选择 DLL 目录；NuGet 包或应用输出已包含 DLL 时不需要此行。
XmaxRuntime.ConfigureNativeDirectory(nativeDirectory);
using var client = new XmaxClient(new XmaxConfiguration
{
    ApiKey = apiKey,
    Environment = XmaxEnvironment.China
});
var capabilities = client.GetCapabilities();
var model = XmaxClient.GetModelInfo();
var cameras = await client.ListCamerasAsync();

await using var realtime = client.CreateRealtime();
realtime.EventReceived += e => Console.WriteLine($"{e.Type}: {e.State}");
await realtime.PrepareCameraAsync(cameras[0].Id, model.DefaultCameraFormat);
await realtime.StartAsync(new GenerationContext("将画面转化为自然的电影风格", referenceImageUrl));
// 可复用连接并更新条件：
await realtime.UpdateAsync(new GenerationContext("改为水彩风格", referenceImageUrl));
await realtime.DisconnectAsync(); // 保留本地媒体和生成条件
await realtime.CloseAsync();      // 释放本地媒体并清除条件；句柄仍可再次 prepare
```

三个 SDK NuGet 包均附带与程序集配套的 XML API 文档；Visual Studio 可据此显示类型、参数和方法提示。公开接口注释及参数名称由 SDK 编译检查，包验证会检查 XML 文件是否存在且属于对应程序集。

摄像头目标尺寸从 `GetModelInfo()` 查询，不应在界面硬编码。当前模型是 x2.1。缺少 RTC 的构建仍可预览和处理外部帧；开始生成会明确报告不支持。

## 线程、取消与所有权

- 阻塞原生调用在后台执行。错误详情在失败的同一线程立即复制，再转换为包含 Result、ApiCode、HttpStatus 的 `XmaxException`。
- `EventReceived` 在托管后台线程串行交付；界面通过自身 Dispatcher 切回 UI。用户事件处理器异常通过 `CallbackFaulted` 报告，不终止事件泵。
- `StartAsync`、`ConnectAsync`、`GenerateAsync` 和 `UpdateAsync` 的取消会断开整个连接。取消摄像头准备会关闭本地媒体；存储取消仅影响该 Storage。取消任务要等原生调用及取消动作真正完成后才结束。WinHTTP 阶段超时仍可能影响等待时间。
- `RequestJsonAsync` 只支持调用前取消；对应 C API 没有进行中的请求取消接口。
- 使用 `await using` 或 `DisposeAsync()` 完成会话关闭。它会拒绝新操作、发起取消/关闭、等待在途调用与事件泵结束，再释放 SafeHandle。单独的 `CloseAsync()` 允许后续重新准备。
- 每个本地/远端视频源只使用一个取帧消费者。`TryAcquireFrame()` 返回拥有所有权的 `XmaxVideoFrame`；用完必须 Dispose，帧可独立于会话存活。`ReadBgra` 的像素指针仅在同步回调期间有效。
- `BindExternalAsync` 返回源绑定的接收器，`PushBgra` 在返回前复制输入。提交时间戳使用严格递增的单调微秒。源被替换或关闭后，旧接收器不会影响新源。
- `SetRemoteMediaListenerAsync` 回调运行在原生交付线程；每次收到的 `XmaxMediaFrame` 已 retain，接收方负责 Dispose。回调应尽快转交数据，不应同步等待会话销毁。解绑之后仍可能有一个在途回调；SDK 完成最后一次回调后才释放托管上下文。

## 存储与日志

`client.CreateStorage()` 创建独立的异步存储对象，支持 UploadFileAsync、UploadDataAsync、DownloadAsync、GetProgress 和 Cancel。每个 Storage 同时只有一次传输。UploadDataAsync 的输入内存在任务完成前不能修改。

`XmaxLogger.Configure` 配置进程级日志，`TryRead` 轮询脱敏后的记录。默认不启用日志。API Key 不应写入示例配置、测试输出或异常附加信息。

## .NET XLab 共同操作

WPF 与 WinUI 3 共用 `Examples/XLabShared` 的会话、预设、参考图上传、凭据和相机偏好，各自提供 UI 调度、计时器与缩略图适配。

首页先选择摄像头并输入 API Key，再点击 Start Camera。多个设备且没有历史选择时须明确选择；保存的设备离线后不会自动切换。模型与摄像头目标尺寸遵循 SDK 的模型能力。界面的自拍显示镜像与上行发布分别设置，具体含义见[镜像说明](usage.md#镜像)。

启动中首页显示 `Starting…`，收到连接或生成状态后进入会话页并开始计时；等待结果首帧时显示 `Generating…`。失败时显示错误，停止会话或关闭窗口会取消进行中的工作并异步释放资源。页面使用淡入与向上移动的过渡；系统关闭动画时直接呈现最终状态。

会话页有 Character Swap、Virtual Try-On、Style Transfer 和 Free Prompt 四种模式。切换模式本身不触发生成，选择参考图或提交提示词才更新条件；快速选择会合并待处理的更新。各模式可上传本地参考图，Free Prompt 可移除参考图；上传中禁用提示词提交，失败可重试。缩略图缓存在本机。顶部提供统计、远端播放音量、计时和停止会话；录制能力见各框架指南。

API Key 保存在当前 Windows 用户的凭据管理器中，重启恢复，清空输入后删除。`XMAX_API_KEY` 可临时覆盖保存值，不改写凭据。顶部按钮使用固定星号表示已设置，展开的编辑框显示完整内容。获取入口为 [Xmax Open Platform](https://platform.xmaxsi.com/api-keys)。两套 Demo 共用凭据与相机偏好，生成和上传使用国内服务。

`--preview` / `--preview-session` 仅检查界面，有明确预览标记，不打开摄像头或连接生成服务；参考图仍从 CDN 加载。启动命令见 [WPF](wpf.md) 和 [WinUI 3](winui.md) 指南。

托管测试与 Demo 冒烟模式覆盖接口、布局与像素交付、页面交互，以及独立凭据条目的保存、恢复、修改和清空，不改写用户的真实凭据。`--smoke-test-camera` 和 `--smoke-test-live` 分别显式验证实际摄像头和云端生成；多设备时可通过 `XMAX_SMOKE_CAMERA` 指定设备名称片段。构建报告记录实际通过的检查，不在文档中固定测试数量。

## 维护与打包

Interop 由公开头文件生成，生成器作为维护者本地工具保留。

不要手工编辑 `.g.cs` 和 `native_abi_probe.c`。生成器覆盖所有 C ABI 导出及公共结构体；高层 C# API 维护在普通 `.cs` 文件。

托管包、Demo ZIP 的构建命令、候选包内容和发布渠道统一见 [本地打包说明](installation.md#本地打包)。这些工具生成本地产物，不会自动发布到 NuGet 或 GitHub。
