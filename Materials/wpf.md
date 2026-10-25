# WPF XLab

`Examples/XLabWpf` 是 .NET 10、x64 的 WPF 示例。界面使用 XAML 和 MVVM，通过 `Xmax.SDK` 调用原生 SDK，通过 `Xmax.SDK.Wpf` 显示视频。Qt XLab 是独立示例，WPF 不依赖 Qt。

`Examples/XLabShared` 保存 WPF 与 [WinUI 3 Demo](winui.md) 共用的 ViewModel、预设目录、凭据和相机偏好；UI 调度、计时器与缩略图解码通过各自的适配器提供。

当前 Parallels 开发环境可以在 macOS 仓库根目录直接运行 `bash scripts/run-wpf.sh`；Windows 可执行 `./scripts/run-wpf.ps1`。默认复用现有 `C:/dev/XmaxSDK-build` 原生构建，托管输出放在 `C:/dev/XmaxSDK-dotnet`，路径均可用参数覆盖。

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

界面按 Qt XLab 的白色首页、棋盘格启动区、双视频会话页和圆形参考图列表实现，共用 Qt 示例的 Logo、DM Sans 字体及 `presets.json`，运行时不依赖 Qt。首页采用 Qt/macOS Demo 同样的启动区宽度规则（约窗口宽度的 45%，限制在 580–940 DIP），固定 1920:1024 比例，并同步缩放标题、Logo 和按钮；窗口变化后重新布局。默认选择模型支持的横向尺寸，摄像头实际输出仍取决于设备支持的格式。

首页与会话页切换时沿用 Qt/macOS 的 450 ms 入场动画：透明度从 0 到 1，同时从下方 14 DIP 回到原位，使用 `(0,0,0.58,1)` ease-out 曲线；返回首页时底部链接同步入场。切换会关闭弹层并回到页面顶部，快速往返会重置旧动画。Windows 关闭客户端动画时直接显示最终状态。

右上角摄像头菜单按 Qt XLab 的圆角列表显示设备，当前设备以蓝色背景和勾选标记突出，选择后关闭菜单并记住设备 ID。多设备且没有历史选择时需要显式选择；记住的设备离线后也不会自动切到另一个源。菜单只包含设备，模型菜单只包含 X2.1；镜像遵循 Demo 默认设置。首页输入 Key 后点击“Start Camera”，会准备摄像头并以 Character Swap 的首张预设启动生成。会话页包含 Character Swap、Virtual Try-On、Style Transfer 和 Free Prompt：切换模式不触发生成，选择参考图或提交提示词才更新条件；快速选择时合并等待中的更新。

各模式支持上传本地参考图，Free Prompt 支持移除参考图。上传中禁用提示词提交，上传失败可点击重试。缩略图按需加载并缓存在用户本机，网络失败显示重试状态；WPF 请求同图的 PNG 缩略图，传给生成服务的参考图 URL 保持原值。

参考图按钮直接处理点击事件，点击图片或名称都会立即更新蓝色选中状态，并通过 SDK 的条件更新接口提交当前参考图。验收会实际调用三类参考图和 Upload 按钮的交互入口；真实生成验收还会点击 Alien Kitty、等待条件更新成功并确认结果视频持续显示。

统计开关、远端音量、计时和停止会话位于顶部工具栏。启动时首页显示 `Starting…`，收到 SDK 的 `Connected` 或 `Generating` 状态后才切到会话页并开始计时；等待结果首帧时显示 `Generating…`。连接失败会留在首页。会话页可停止连接或生成，关闭窗口也会取消启动并异步释放资源。Record 按钮保留 Qt 的位置，但当前 WPF 版尚未接入录制，按钮禁用并有说明。

API Key 输入后自动保存在当前 Windows 用户的凭据管理器中，重启 Demo 会恢复；清空输入框会删除保存的 Key。`XMAX_API_KEY` 环境变量可临时覆盖已保存的值，不改写本机凭据。输入框下沿用 macOS Demo 的获取提示，点击 [Xmax Open Platform](https://platform.xmaxsi.com/api-keys) 可进入平台的 API Keys 页面。Demo 的生成与图片上传固定使用国内服务。改变凭证、摄像头或尺寸后，下一次预览/生成会重新准备会话。

只查看 UI 时可执行 `./scripts/run-wpf.ps1 -Preview` 或 `-PreviewSession`（macOS 使用 `bash scripts/run-wpf.sh -PreviewSession`）。此模式有明确的预览标识，不创建摄像头或生成会话；参考图仍会从 CDN 加载。

## 控件接入

```xml
<sdk:XmaxVideoView
    xmlns:sdk="clr-namespace:Xmax.SDK.Wpf;assembly=Xmax.SDK.Wpf"
    VideoSource="{Binding Session.LocalVideo}"
    ContentMode="Fit" />
```

`VideoSource` 接受 SDK 的 LocalVideo 或 RemoteVideo。后台仅保留最新帧，WPF 渲染节拍读取并直接复制到复用的 WriteableBitmap；不为每一帧排入 Dispatcher 队列，也不分配中间像素数组。首版显示 BGRA；SDK 原始帧的镜像标记只应用一次。控件卸载或切换 Source 会停止帧泵并释放待显示帧。

不要为同一个 SDK 邮箱同时绑定多个独立控件；需要多视图时应先实现帧分发。WPF 控件不负责关闭会话，应用负责 DisposeAsync。

## 验证

```powershell
./scripts/verify-dotnet.ps1 -NativeDirectory <native-dll-directory> -Camera
# 真实云端验收：通过安全的本机方式设置环境变量后执行，不把 Key 放进命令行。
./scripts/verify-dotnet.ps1 -NativeDirectory <native-dll-directory> -Live
```

验收脚本默认执行托管测试、ABI 检查、`--smoke-test`、`--smoke-test-ui`（首页、会话页、自由提示词截图及模式交互）、`--smoke-test-key-gate`（缺少 Key 时的输入弹层），以及四次独立进程的 Key 保存、恢复、修改和清空检查。凭据测试使用独立的临时条目，UI 预览和其他冒烟测试不会改写用户保存的 Key。`-Camera` 增加真实预览；`-Live` 增加使用首页默认预设的真实生成及自由提示词更新。需要截图和 JSON 报告时设置 `XMAX_SMOKE_OUTPUT`。真实生成测试从首页直接启动，并检查本地预览可见且包含非单色像素；测试完成或失败均关闭会话。多设备环境可用 `XMAX_SMOKE_CAMERA` 指定测试设备名称片段（如 `MacBook`），避免测试到未接入画面的虚拟摄像头。

项目结构与 C# 接口说明见 [C# SDK](dotnet.md)。
