# 文档与素材

本目录沿用 macOS SDK 的 `Materials` 组织方式，作为可提交的公开文档与素材目录。

- [安装说明](installation.md)：CMake 构建、Windows DLL、Agora、Qt 示例与 Mac 开发验证。
- [使用说明](usage.md)：C/C++ 接入、SDK 摄像头与外部输入、存储、画质、帧交付与生命周期。
- `images/brand/`：共享 XmaxSDK 品牌与设计素材。
- `images/use-cases/` 与 `videos/use-cases/`：共享模型演示海报和视频。
- `images/why/`：共享产品介绍插图。
- `images/xlab/`：从 macOS 工程 Materials 沿用的 **iOS XLab** 截图与录屏，用于设计参考。Windows SDK 的 Qt 示例源码位于 [Examples/XLab](../Examples/XLab)。

品牌、插图和演示媒体复制自用户提供的 macOS 工程；安装与使用说明根据当前 Windows C++ / C API 实现编写。共享演示素材不表示 Windows SDK 已实现相应输入 API。当前 SDK 提供摄像头与外部视频输入；静态图片/文件视频独立解码、鼠标轨迹尚未实现；远端插帧不在 SDK 范围内。Qt 只负责 Demo 界面与交互。

工程根目录为 `Windows/XmaxSDK`，主入口见 [README](../README.md)。内部设计文档保存在 Git 忽略的 `Docs` 中；本目录不在忽略规则内。
