# C# SDK

`Sources/XmaxSDK.Net` 提供通用 C# 封装，程序集和命名空间为 `Xmax.SDK`。依赖 .NET 10；Windows 原生 SDK 仅支持 x64 进程。该类库不依赖 WPF、Qt 或 C++/CLI。

界面适配分为 `Sources/XmaxSDK.Wpf`（`Xmax.SDK.Wpf`）与 `Sources/XmaxSDK.WinUI`（`Xmax.SDK.WinUI`）。两者使用同一个视频源和后台最新帧队列，分别通过 WPF / WinUI 3 的位图和渲染节拍显示视频。示例工作流统一放在 `Examples/XLabShared`，通过调度器、计时器和缩略图接口接入两套 UI。

解决方案入口是根目录 `XmaxSDK.DotNet.sln`，可以使用 .NET CLI 或支持 .NET 10 的 Visual Studio 2026；现有原生 SDK 仍可使用 Visual Studio 2022 构建。

## 构建与测试

先按 installation.md 构建原生 SDK，再从仓库根目录执行：

```powershell
./scripts/verify-dotnet.ps1 -NativeDirectory .build/windows/Sources/XmaxSDK/Release
```

在虚拟机共享源码目录开发时，输出必须位于 Windows 本地磁盘：

```powershell
./scripts/verify-dotnet.ps1 `
  -NativeDirectory C:/dev/XmaxSDK-build/Sources/XmaxSDK/RelWithDebInfo `
  -BuildRoot C:/dev/XmaxSDK-dotnet
```

脚本比较原生 C 编译器和 C# 的所有公共结构体大小、字段偏移及事件枚举值，运行真实 DLL 的托管集成测试、WPF 像素/镜像/关闭测试，以及不使用摄像头或生成服务的界面冒烟测试。界面测试会加载预设缩略图。`-Camera` 显式打开摄像头；`-Live` 额外创建真实云端生成会话，需要通过 `XMAX_API_KEY` 提供凭证。这两个开关默认关闭。

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

## 维护与打包

Interop 由公开头文件生成：

```sh
python .cicd/generate-dotnet-interop.py
python .cicd/generate-dotnet-interop.py --check
```

不要手工编辑 `.g.cs` 和 `native_abi_probe.c`。生成器覆盖所有 C ABI 导出及公共结构体；高层 C# API 维护在普通 `.cs` 文件。

`verify-dotnet.ps1 -Package` 生成 `Xmax.SDK`、`Xmax.SDK.Wpf` NuGet 包和 WPF Demo ZIP，并在独立目录安装 NuGet 包验证原生库加载。版本来自根目录 VERSION。发布时使用 Release/RelWithDebInfo 原生 DLL 和匹配的 Agora 运行库，保留所有依赖许可证；该脚本只生成本地产物，不发布到 NuGet 或 GitHub。

使用已解压的原生发布包的 `bin/` 作为 NativeDirectory 时，会自动把相邻的 `licenses/` 纳入托管包和 Demo。使用其他开发目录打包时，可用 `-DependencyNoticesDirectory` 指定原生依赖的许可证目录。
