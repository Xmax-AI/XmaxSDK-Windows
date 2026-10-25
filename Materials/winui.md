# WinUI 3 XLab

`Sources/XmaxSDK.WinUI` 提供 `Xmax.SDK.WinUI` 视频控件；`Examples/XLabWinUI` 是独立 WinUI 3 Demo。它们直接使用通用 `Xmax.SDK`，不加载 WPF 或 Qt。当前使用稳定版 Windows App SDK 2.5.1，面向 Windows x64、.NET 10，最低 Windows 10 2004（19041）。编译目标 Windows SDK 为 26100。

## 启动

安装 .NET 10 SDK，准备带 Agora 的原生 SDK 及 Microsoft Visual C++ x64 Redistributable；NuGet 会还原 Windows App SDK 和构建工具，无需先安装 Visual Studio 的 WinUI 模板。

```powershell
./scripts/run-winui.ps1 -NativeDirectory C:/dev/XmaxSDK-build/Sources/XmaxSDK/RelWithDebInfo
# 不打开摄像头或连接生成服务的界面预览：
./scripts/run-winui.ps1 -Preview
./scripts/run-winui.ps1 -PreviewSession
```

Parallels 共享源码环境可在 macOS 仓库目录执行 `bash scripts/run-winui.sh`。默认共享路径为 `\\Mac\XmaxSDK`，构建输出为 Windows 本地的 `C:/dev/XmaxSDK-dotnet`，可用脚本参数覆盖。

Demo 为 unpackaged 桌面应用，直接运行 `XLabWinUI.exe`。发布包包含匹配的 Windows App SDK 运行文件，仍需安装 .NET 10 Runtime 和原生 SDK 依赖的 Visual C++ 运行库。

## UI 与共享逻辑

首页、双视频页、圆形参考图、四种模式、顶部统计与音量、API Key 输入、弹层和白色配色对齐 WPF / Qt XLab，共用 Logo、DM Sans 和预设 JSON。首页启动区遵循 580–940 DIP 宽度限制与 1920:1024 比例；顶部按钮到视频的可见间距为 20 DIP。窗口 API 的物理像素与界面的 DIP 分开换算，适配系统缩放。

页面切换沿用 450 ms、从下方 14 DIP 入场的淡入与 ease-out 动画，返回首页时底部链接同步过渡；系统关闭动画时直接显示最终状态。三类参考图各自保留滚动位置，点击后更新蓝色选择状态和生成条件。Free Prompt 支持输入、提交、上传和移除参考图。Record 位置与当前 WPF Demo 一致，尚未接入录制，保持禁用并显示说明。

点击 Start Camera 后首页显示 `Starting…`，收到 SDK 的 `Connected` 或 `Generating` 状态后才切到会话页并开始计时，连接失败留在首页；等待结果首帧时显示 `Generating…`。顶部 Key 按钮以居中的星号显示已保存状态，展开后使用明文输入框编辑完整 Key。

`Examples/XLabShared` 保存两套 .NET Demo 的会话生命周期、串行条件更新、参考图上传、预设和凭据逻辑。各框架只提供 UI 调度器、计时器和缩略图解码适配。API Key 与相机选择沿用 WPF 的现有存储位置，因此已保存的选择可以直接使用；输入后自动记忆，清空输入则删除。环境变量 `XMAX_API_KEY` 为临时覆盖，不改写保存的 Key。生成和上传固定使用国内服务，Key 输入下提供平台获取入口。

## 控件接入

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
./scripts/verify-winui.ps1 -NativeDirectory <native-dll-directory>
./scripts/verify-winui.ps1 -NativeDirectory <native-dll-directory> -Camera
# 显式设置 XMAX_API_KEY 后，额外验证真实生成与参考图/提示词更新：
./scripts/verify-winui.ps1 -NativeDirectory <native-dll-directory> -Live
./scripts/pack-winui.ps1 -NativeDirectory <native-dll-directory>
```

默认验证实际 BGRA 像素、镜像、快速帧替换、卸载重载、页面截图与动画、参考图按钮，以及 API Key 的四次独立进程保存/恢复/修改/清空。测试使用独立凭据条目，其他冒烟模式不会改写用户保存的 Key。`-Live` 从首页直接启动摄像头和生成，再实际点击 Alien Kitty 并提交自由提示词更新。`XMAX_SMOKE_CAMERA` 可指定摄像头名称片段；报告和截图保存到构建目录的 `winui-screenshots/`。

打包输出为 `Xmax.SDK.WinUI.<version>.nupkg` 与 `XLabWinUI-<version>-win-x64.zip`，并更新 `SHA256SUMS`。发布包保留字体、原生依赖和托管依赖许可证。构建与打包均不发布到 NuGet 或 GitHub。
