# WPF XLab

`Examples/XLabWpf` 是 .NET 10、x64 的 WPF 示例。界面使用 XAML 和 MVVM，通过 `Xmax.SDK` 调用原生 SDK，通过 `Xmax.SDK.Wpf` 显示视频。Qt XLab 是独立示例，WPF 不依赖 Qt。

`Examples/XLabShared` 保存 WPF 与 [WinUI 3 Demo](winui.md) 共用的 ViewModel、预设目录、凭据和相机偏好；UI 调度、计时器与缩略图解码通过各自的适配器提供。

## 启动

安装 .NET 10 SDK/Windows Desktop Runtime、Microsoft Visual C++ x64 Redistributable，并准备原生 SDK。实时生成需要包含 Agora 的构建及匹配 DLL。

```powershell
dotnet run --project Examples/XLabWpf -c Release `
  -p:XmaxNativeDir=C:/dev/XmaxSDK-build/Sources/XmaxSDK/RelWithDebInfo
```

共享源码的虚拟机环境额外指定本地构建目录：

```powershell
dotnet run --project Examples/XLabWpf -c Release `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/ `
  -p:XmaxNativeDir=C:/dev/XmaxSDK-build/Sources/XmaxSDK/RelWithDebInfo
```

摄像头选择、API Key、启动与模式切换、参考图上传和预览模式的操作统一见 [C# SDK 的共同操作说明](dotnet.md#net-xlab-共同操作)。

WPF 缩略图使用同图的 PNG 资源，提交给生成服务的参考图 URL 保持原值。当前 WPF 的 Record 按钮未接入录制，保持禁用。需要录制结果可使用 [WinUI 3 XLab](winui.md) 或 Qt XLab。

只查看 UI 时，在上述 `dotnet run` 命令末尾添加 `-- --preview` 或 `-- --preview-session`。预览不打开摄像头或连接生成服务。

## 控件接入

旧版 `XmaxVideoView` 已更名为 `XmaxRealtimeVideoView`，迁移时替换 XAML 标签和 C# 类型引用。包名、命名空间和绑定属性保持一致，旧类型名不再提供；详见 [1.0.0 变更说明](https://github.com/Xmax-AI/XmaxSDK-Windows/releases/tag/1.0.0#api-changes)。

```xml
<sdk:XmaxRealtimeVideoView
    xmlns:sdk="clr-namespace:Xmax.SDK.Wpf;assembly=Xmax.SDK.Wpf"
    VideoSource="{Binding Session.LocalVideo}"
    ContentMode="Fit" />
```

`VideoSource` 接受 SDK 的 LocalVideo 或 RemoteVideo。后台仅保留最新帧，WPF 渲染节拍读取并直接复制到复用的 WriteableBitmap；不为每一帧排入 Dispatcher 队列，也不分配中间像素数组。首版显示 BGRA；SDK 原始帧的镜像标记只应用一次。控件卸载或切换 Source 会停止帧泵并释放待显示帧。

不要为同一个 SDK 邮箱同时绑定多个独立控件；需要多视图时应先实现帧分发。WPF 控件不负责关闭会话，应用负责 DisposeAsync。

## 验证

```powershell
dotnet run --project Tests/XmaxSDKWpfTests -c Release `
  -p:XmaxNativeDir=C:/path/to/sdk/bin `
  -p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/
```

通用验收行为与环境变量见 [C# SDK](dotnet.md)。WPF 验证包含 WriteableBitmap 像素、镜像和关闭资源检查；设置 `XMAX_SMOKE_OUTPUT` 可指定截图与 JSON 报告目录。
