<p align="center">
  <img src="./Materials/images/brand/xmax-sdk.png" alt="XmaxSDK — Realtime Interactive Video Generation" width="880">
</p>

# XmaxSDK for Windows

C++17 核心、稳定 C ABI 和 Qt 6 / QML X-Lab 示例。当前开发版本为 `0.2.0`，C ABI 版本仍为 `1`。

功能职责与分层参考 macOS SDK；示例沿用 macOS XLab 的布局、字体、预设和交互。摄像头、帧处理、RTC、画质策略与存储均由 SDK 实现。Qt 只负责 Demo 界面、用户操作和 SDK 帧的显示。

## 当前能力

- Client / Realtime / Storage 的 C 句柄、能力查询、错误码、线程局部错误信息，以及可选 C++ RAII 包装。
- SDK 摄像头枚举、准备、首帧就绪、切换和停止；Windows 使用 Media Foundation，Mac 开发使用 AVFoundation。
- 外部 BGRA、NV12、I420 视频输入；显式 stride 和可访问字节数校验；同步复制像素。
- 独立连接、开始生成、条件更新、条件复用、HTTP / RTC 双心跳及失败回滚。
- 上下行独立的五档自适应画质、按模型持久化历史、编码码率与帧率偏好、网络和设备性能事件。
- SDK 内部 STS / COS 签名、图片及视频上传、可选图片安全检查、下载、进度查询和取消。
- 本地与远端最新帧邮箱；Windows 提供 CPU / GDI HWND 显示接口，支持 fit / fill。
- 与现有服务端一致的 UTF-8 信令分片、顺序发送和限速；平台字段为 `windows`。
- 串行生命周期工作线程、取消检查、失败回滚、重复关闭、最新远端帧邮箱。
- 可选 Agora Windows 4.6.2 原生适配：自定义视频输入、麦克风、目标 bot 帧过滤、音量、质量统计。
- Windows 使用 WinHTTP；macOS 开发构建使用系统 libcurl。默认不连接任何服务。
- CMake 安装与 `find_package(XmaxSDK)`；独立 C/C++ 消费端测试。

当前采用 CPU 帧路径。Windows 插帧后端、D3D11 纹理零拷贝、外部音频输入和轨迹交互尚未实现；启用插帧返回 `XMAX_UNSUPPORTED`，能力标志不会宣称支持。画质历史按模型和上下行分别持久化到用户缓存，保留一小时；缓存不可写时回退到默认策略。静态图片和文件视频解码未提供独立输入接口，可通过宿主解码后提交外部帧。

## 工程结构

工程根目录为 `Windows/XmaxSDK`。以下命令均在该目录执行。

```text
XmaxSDK/
  CMakeLists.txt     Windows / macOS 开发构建入口
  include/xmax/     C ABI 与 C++ RAII 头文件
  Sources/XmaxSDK/
    CAPI/            C 句柄、参数转换和异常边界
    Core/            Client、Realtime / Storage Manager、生命周期协调器
    Service/         API、Session、媒体规格、STS 与存储协议
    Stream/          RTC 房间、生成信令、分片限速、编码与自适应画质
    Media/           摄像头与外部视频控制、时间戳、帧提交和排空
    Render/          本地/远端预览、绑定隔离、最新帧交付
    Foundation/      帧转换、原生采集、窗口绘制、HTTP、加密、RTC 适配
  Tests/XmaxSDKTests/ 协议、分层、生命周期及独立消费端测试
  Examples/XLab/      QML 界面、字体、Logo 和 60 个预设
  Materials/          公开安装/使用说明、共享品牌与演示素材
  Docs/               本地内部设计文档，由 /Docs/ 忽略
  cmake/              安装包配置与依赖方向检查
  third_party/        随源代码提供的依赖及许可证
  scripts/            本地验证、预览、Agora 依赖下载
  .github/workflows/  macOS / Windows 核心和 Windows 完整示例构建
```

各层有独立 CMake target。`Core` 组合 `Service / Stream / Media / Render`；这四层各自依赖 `Foundation`，彼此通过 Core 连接。配置与测试检查依赖方向、SDK 不包含 Qt 头文件，以及示例只通过公开 API 接入。

公开文档与素材入口为 [Materials](Materials/README.md)，包含 [安装说明](Materials/installation.md) 和 [使用说明](Materials/usage.md)。`Docs` 只保存本地内部文档，公开文档不依赖其内容。

## 只构建 SDK

需要 CMake 3.24+ 和支持 C++17 的编译器。Windows 推荐 Visual Studio 2022 的 Desktop development with C++ 工作负载和 Windows SDK。

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
cmake --install .build/core --prefix .build/install
```

Windows 的 Visual Studio 生成器：

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

默认 `XMAX_WITH_AGORA=OFF`，允许离线构建和测试。缺少 RTC 后端时，开始生成明确返回 `XMAX_UNSUPPORTED`，在创建服务端 Session 之前失败。

## Windows X-Lab 与真实 RTC

安装 Qt 6.8+ 的 MSVC x64 构建，包含 Qt Quick、Quick Controls、Multimedia、Image Formats（预设缩略图使用 WebP）。SDK 与 Qt 必须使用相同的架构和 MSVC 工具链。

在 PowerShell 中下载固定版本 Agora，然后构建：

```powershell
$agora = ./scripts/get-agora.ps1
$qt = "C:/Qt/6.8.3/msvc2022_64"
cmake --preset windows -DXMAX_BUILD_EXAMPLE=ON -DXMAX_WITH_AGORA=ON "-DXMAX_AGORA_ROOT=$agora" "-DCMAKE_PREFIX_PATH=$qt"
cmake --build --preset windows
& "$qt/bin/windeployqt.exe" --release --qmldir Examples/XLab/qml .build/windows/Examples/XLab/Release/XLab.exe
& .build/windows/Examples/XLab/Release/XLab.exe
```

构建会复制 XmaxSDK 和 Agora DLL；`windeployqt` 部署 Qt 运行库、QML 模块、图片及多媒体插件。交付时保留依赖附带的许可证。Agora 下载脚本校验固定 SHA-256，不提交二进制依赖。

## Mac 开发与界面预览

安装 Qt 6.8+ macOS 构建后，可以运行相同的 C++ 示例。当前 macOS 构建用于公共逻辑和 UI 验证，没有打包 Agora macOS C++ 后端。

不带参数启动为正常模式：点击 Start Camera 时，空白 API Key 会拦截并打开输入框；填入 Key 后，当前 Mac 构建会提示实时视频不可用并留在首页。
`--preview` / `--preview-session` 是明确的 UI 预览入口，不使用 API Key、不开摄像头、不连接生成服务；标题、按钮和视频面板均标注预览状态。

```sh
cmake --preset xlab -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos
cmake --build --preset xlab
# 正常启动，验证 API Key 拦截与当前构建的能力提示：
bash scripts/run-xlab.sh
# 仅检查布局与交互：
bash scripts/run-xlab.sh --preview
# 直接检查会话页面：
bash scripts/run-xlab.sh --preview-session
```

本次工作区已把构建工具与 Qt 安装在忽略的 `.build/` 下，可直接运行：

```sh
bash scripts/verify.sh --example
bash scripts/run-xlab.sh
```

预设缩略图和生成参考图与当前 Web 示例一致，使用 `https://cdn.22duck.cn/images/source/`；缩略图读取 PNG/WebP，成功加载后缓存到本机。加载中显示进度，失败时显示警告图标与具体提示，点击可重试。
图片域名不可达时无法显示实际缩略图，不影响预设的生成参考地址。自动化还单独验证无 Key 拦截、PNG/WebP 解码和失败重试；UI 预览通过不代表真实会话验收通过。

预览模式不打开摄像头、不创建付费会话、不伪造生成结果或指标。参考图缩略图仍通过 HTTPS 加载。它可以检查模式、提示词、统计显隐、工具栏、弹层和停止交互。

## 接入与生命周期

公开头文件为 `include/xmax/xmax.h`。所有字符串使用 UTF-8。跨 DLL 的资源由对应 SDK release/destroy 函数释放，C++ 异常不会穿过 C ABI。

```cpp
#include <xmax/xmax.hpp>

xmax::client client(apiKey);
auto realtime = client.create_realtime();
realtime.prepare_camera(); // SDK 启动本地摄像头；默认 1920x1024@30，不创建云端会话
// start 会阻塞到首个目标结果帧或超时，应从后台线程调用。
realtime.start("视频中角色替换成参考图中角色", referenceURL);
// 也可以先 realtime.connect()，再 realtime.generate(prompt, referenceURL)。
```

客户端销毁后，已创建的 Realtime / Storage 仍持有自己的配置。`disconnect()` 结束服务端和 RTC 会话，保留本地媒体、预览与生成条件；`close()` 同时释放本地媒体并清除条件。二者均为阻塞操作，可从另一线程取消进行中的启动。HTTP 取消受 WinHTTP 网络阶段超时约束。

需要自有采集时，调用 `prepare({1920,1024,30})`，由宿主持续提交外部帧。模型输入规格为 `1920x1024` 或 `1024x1920`、`1...60 fps`。帧宽高必须等于协商规格，时间戳为严格递增的单调微秒时间；push 返回后可复用像素。替换外部输入前，宿主应停止旧源并等待提交结束。

`poll_event()`、`acquire_frame()` 与 `acquire_local_frame()` 非阻塞。帧由独立句柄持有，读完调用 `xmax_frame_release()`；C++ RAII 自动释放。旧帧被最新帧替换，避免积压。状态快照与配置查询经过生命周期队列，可能等待其他操作完成。销毁句柄前须停止并等待使用该句柄的宿主线程。

X-Lab 的 Stop Session 先返回启动页，再在后台关闭 SDK。API Key 与 macOS 示例一样保存在当前用户设置中；预览模式不读写它。快速选择参考图采用串行更新和最新待处理选择；切换分类不自动生成，并保留各分类滚动位置。虚拟试衣发送 `face_replace=true`。

## 验证范围

自动测试不使用真实凭证和摄像头。覆盖依赖方向、各层独立运行、协议分片、帧所有权、采集排空、摄像头切换/失败/取消、连接与生成解耦、调档冷却与信令、SHA-1/HMAC 签名、存储取消/文件提交、C ABI 布局与 C++ RAII，以及 Qt UI 交互。

当前已在 Apple Silicon Mac 构建与运行。Windows 原生分支已用 Windows x64 GNU 交叉工具链编译并链接出 `XmaxSDK.dll`（关闭 Agora）；Agora 适配器对照实际 Windows 4.6.2 头文件通过语法检查。Windows MSVC 的核心、原生设备枚举/窗口接口、Agora + Qt、安装包消费验证由远端 CI 执行。真实摄像头、COS、云端生成和持续性能仍需实机验收。

## CI/CD 与候选包

远端仓库为 [XingMai/XmaxSDK-Windows](https://github.com/XingMai/XmaxSDK-Windows)。
[.cicd 流程说明](.cicd/README.md) 包含版本准备、原子快进集成、Windows MSVC 打包、解压消费端验证和发布演练。

日常 CI 覆盖 macOS 核心/ASan/UBSan、Windows 核心、Agora + Qt、C ABI 导出、安装包 C/C++ 消费端和 .NET P/Invoke。
通过后生成 SDK ZIP、可运行的 XLab ZIP、SHA256SUMS 和源码提交验证记录。
手动执行 SDK Release 时，`publish=false` 仅生成 Actions Artifact；正式发布必须显式开启，并匹配 main、版本标签和 Release Notes。标签推送本身不会发版。
