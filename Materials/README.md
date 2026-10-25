# 文档与素材

## 接入文档

- [安装说明](installation.md)：原生 SDK 构建、二进制包接入、Qt 示例和 Parallels 开发。
- [使用说明](usage.md)：C/C++ 接口行为、媒体规格、线程与所有权，是原生接入规则的主文档。
- [C# SDK](dotnet.md)：托管接口、取消和所有权，以及两套 .NET Demo 的共同操作。
- [WPF XLab](wpf.md)、[WinUI 3 XLab](winui.md)：各自的控件接入、运行方式和功能差异。
- [本地打包](installation.md#本地打包)：托管包、Demo ZIP 与运行依赖。

接口的精确契约见[公开头文件](../include/xmax/xmax.h)。项目结构和文档维护分工见[项目入口](../README.md)。

## 素材

- `images/brand/`：共享品牌素材。
- `images/use-cases/` 与 `videos/use-cases/`：模型演示海报和视频。
- `images/why/`：产品介绍插图。
- `images/xlab/`：iOS XLab 截图与录屏，供界面设计参考。

素材文件不代表 SDK 提供对应的媒体解码接口；支持范围见使用说明。Qt 示例源码位于 [Examples/XLab](../Examples/XLab)。
