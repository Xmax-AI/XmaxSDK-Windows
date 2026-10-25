# WinUI 3 XLab

`Sources/XmaxSDK.WinUI` 提供 `Xmax.SDK.WinUI` 视频控件；`Examples/XLabWinUI` 是独立 WinUI 3 Demo。它们直接使用通用 `Xmax.SDK`，不加载 WPF 或 Qt。面向 Windows x64、.NET 10，最低 Windows 10 2004（19041）。Windows App SDK 版本和编译目标由[项目文件](../Sources/XmaxSDK.WinUI/XmaxSDK.WinUI.csproj)维护。

## 启动

安装 .NET 10 SDK，准备带 Agora 的原生 SDK 及 Microsoft Visual C++ x64 Redistributable；NuGet 会还原 Windows App SDK 和构建工具，无需先安装 Visual Studio 的 WinUI 模板。

```powershell
dotnet build Examples/XLabWinUI -c Release -p:Platform=x64 `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/
& C:/dev/XmaxSDK-dotnet/bin/XLabWinUI/x64/Release/net10.0-windows10.0.26100.0/win-x64/XLabWinUI.exe
```

可在运行命令末尾添加 `--preview` 或 `--preview-session`，只查看界面。共享源码环境的构建输出必须位于 Windows 本地磁盘。

Demo 为 unpackaged 桌面应用，直接运行 `XLabWinUI.exe`。发布包包含匹配的 Windows App SDK 运行文件，仍需安装 .NET 10 Runtime 和原生 SDK 依赖的 Visual C++ 运行库。

## 操作与录制

通用界面操作、API Key 显示与存储、摄像头选择、页面状态和参考图更新统一见 [C# SDK 的共同操作说明](dotnet.md#net-xlab-共同操作)。WinUI 按系统缩放区分窗口物理像素与界面 DIP。

会话结果就绪后可点击 Record，录制远端结果的视频和音频。再次点击停止录制，完成编码后选择 MP4 保存位置。录制使用原始结果媒体时间线，显示镜像和播放音量不改变保存内容；结果不含音轨时可保存纯视频。

录制进行中显示计时，结束时显示收尾状态。取消保存对话框会丢弃本次录制；保存失败时可选择其他位置重试，或丢弃录制。停止会话和关闭窗口时也会处理进行中的录制及待保存内容。

## 控件接入

旧版 `XmaxVideoView` 已更名为 `XmaxRealtimeVideoView`，迁移时替换 XAML 标签和 C# 类型引用。包名、命名空间和绑定属性保持一致，旧类型名不再提供；详见 [1.0.0 变更说明](https://github.com/Xmax-AI/XmaxSDK-Windows/releases/tag/1.0.0#api-changes)。

在 WinUI 项目中引用 `Xmax.SDK.WinUI`，并保持项目的 `UseWinUI` 配置：

```xml
<sdk:XmaxRealtimeVideoView
    xmlns:sdk="using:Xmax.SDK.WinUI"
    VideoSource="{Binding LocalVideo}"
    ContentMode="Fit" />
```

`VideoSource` 可绑定实时会话的 `LocalVideo` 或 `RemoteVideo`。控件后台只保留最新帧，在 WinUI 渲染节拍中写入复用的 WriteableBitmap，支持 Fit / Fill 与 SDK 的镜像标记。帧在复制完成后释放；卸载或替换源时停止旧帧泵。一个视频源对应一个取帧消费者，多视图需要先实现帧分发。会话由调用方通过 `DisposeAsync()` 关闭。

## 验证与打包

```powershell
dotnet run --project Tests/XLabRecordingTests -c Release -p:Platform=x64 `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/ -- --native
```

界面冒烟模式可直接传给上面的 Demo EXE：`--smoke-test-recording`、`--smoke-test-frames`、`--smoke-test-ui`。`--smoke-test-camera` 打开实际摄像头，`--smoke-test-live` 还会创建真实云端生成会话，需显式提供 `XMAX_API_KEY`。

通用验收行为与环境变量见 [C# SDK](dotnet.md)。WinUI 另外验证控件卸载重载、结果录制、编码收尾和保存流程；报告与截图保存到构建目录的 `winui-screenshots/`。

包内容、依赖许可证和发布渠道见 [本地打包说明](installation.md#本地打包)。
