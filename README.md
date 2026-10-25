<p align="center">
  <img src="./Materials/images/brand/xmax-sdk.png" alt="XmaxSDK — Realtime Interactive Video Generation" width="880">
</p>

# XmaxSDK for Windows

XmaxSDK 提供实时视频生成、媒体输入、结果音视频交付、轨迹交互和云存储能力。原生 SDK 使用 C++17 核心，通过稳定 C ABI 对外提供接口，不依赖界面框架。当前开发版本为 `0.2.0`，C ABI 版本为 `1`。

C/C++ 应用可直接使用原生 SDK；C# 应用通过 `Xmax.SDK` 接入。WPF 和 WinUI 3 提供可选的 `XmaxRealtimeVideoView` 控件。Qt、WPF、WinUI 3 的 XLab 均为独立 Demo。

## 接入入口

| 目的 | 文档 |
| --- | --- |
| 原生 SDK 安装、源码构建、Qt Demo 与 Parallels 调试 | [安装说明](Materials/installation.md) |
| C/C++ 接入、媒体规格、生命周期、线程与所有权 | [使用说明](Materials/usage.md) |
| C# 接口、异步调用、取消与共享 Demo 操作 | [C# SDK](Materials/dotnet.md) |
| WPF 控件与 Demo | [WPF XLab](Materials/wpf.md) |
| WinUI 3 控件、Demo 与录制 | [WinUI 3 XLab](Materials/winui.md) |
| 本地打包、版本准备和 GitHub 上传 | [CI/CD](.cicd/README.md) |
| 版本变更 | [Release Notes](.cicd/release-notes) |

精确的参数、返回值、并发与资源约束以[公开 C 头文件](include/xmax/xmax.h)为准；[C++ 入口](include/xmax/xmax.hpp)提供可选的 RAII 包装。模型支持的尺寸与能力应通过 SDK 查询，接入示例见使用说明。

## 工程结构

```text
XmaxSDK/
  include/xmax/         C ABI、C++ 类型与 RAII 包装
  Sources/XmaxSDK/
    CAPI/               C 句柄、参数转换和异常边界
    Core/               Client、实时生命周期与存储管理
    Service/            API、Session、模型与媒体规格、存储协议
    Stream/             RTC 房间、信令、编码与自适应画质
    Media/              摄像头、外部媒体输入与采集生命周期
    Render/             预览、远端媒体交付、轨迹缓存与呈现
    Foundation/         原生采集、帧转换、HTTP、加密与平台适配
  Sources/XmaxSDK.Net/   通用 C# 封装
  Sources/XmaxSDK.Wpf/   WPF 视频控件
  Sources/XmaxSDK.WinUI/ WinUI 3 视频控件
  Examples/             Qt、WPF、WinUI 3 Demo 与消费端样例
  Tests/                原生 SDK 测试
  Materials/            公开接入文档与素材
  .cicd/                本地构建、验包与发布工具
```

CMake 检查原生各层的依赖方向及 SDK 与 Qt 的隔离。`Examples/XLabShared` 保存两套 .NET Demo 的共享业务逻辑。内部设计和编码规范保存在 Git 忽略的 `Docs/`，公开接入文档不依赖该目录。

## 文档维护

README 只提供项目概览和入口；安装命令集中在安装说明，原生接口行为集中在使用说明，托管接口与通用 Demo 操作集中在 C# SDK 文档，本地打包与上传规则集中在 CI/CD 文档。框架指南只补充各自的接入方式和差异，Release Notes 只记录版本变化。

版本号由 `VERSION` 驱动，版本准备工具同步更新文档中的版本标记，源码检查验证一致性。依赖下载版本与校验值由 [.cicd/dependencies.json](.cicd/dependencies.json)维护；其他文档中的本机依赖路径仅为配置示例。验证记录应随构建产物保存，不在接入文档中维护测试数量或历史性能快照。

源码仓库：[Xmax-AI/XmaxSDK-Windows](https://github.com/Xmax-AI/XmaxSDK-Windows)。
