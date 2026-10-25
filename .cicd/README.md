# 本地打包与发布

流程为：本地验证和打包，推送代码与版本标签，再上传 GitHub Release 附件。不使用 GitHub Actions，也不等待远端 CI 检查。仓库为 `Xmax-AI/XmaxSDK-Windows`。

原生环境配置见[安装说明](../Materials/installation.md)，托管环境见 [C# SDK](../Materials/dotnet.md)。依赖版本和下载校验值由 [dependencies.json](dependencies.json)维护。本页统一维护打包命令和发布规则。

## 准备版本

```sh
./.cicd/ci-prepare.sh 0.3.0 --dry-run
./.cicd/ci-prepare.sh 0.3.0
```

PowerShell 使用同名 `.ps1`。版本准备工具更新 `VERSION`、文档版本标记，并创建该版本的 Release Notes；不会提交、推送或创建标签。可以在 main 或开发分支准备版本。填写 Release Notes 后提交代码，打包要求工作区干净，确保附件对应一个确定的提交。

## 本地验证与打包

快速检查在 macOS 或 Windows 上均可运行：

```sh
python3 .cicd/check-source.py
python3 .cicd/test-workflow.py
```

在 Windows 工程根目录执行完整打包。共享源码环境的构建目录必须位于 Windows 本地磁盘；下面的 Qt 和 Agora 路径为配置示例：

```powershell
python .cicd/pipeline.py build-windows `
  --build-dir C:/dev/XmaxSDK-release `
  --output-dir .build/artifacts `
  --qt-prefix C:/Qt/6.8.3/msvc2022_64 `
  --agora-root C:/dev/XmaxSDK-deps/agora/sdk
```

该命令完成 MSVC Release 构建、原生测试、DLL 导出与依赖检查、安装包消费、Qt 界面验证、C# ABI 和集成测试、WPF/WinUI 控件与界面验证、录制测试，以及 NuGet 消费验证。默认不打开摄像头、不创建云端生成会话。原生 SDK 和三套 Demo 使用相同的原生 DLL；托管包与 Demo 从原生安装包收集依赖说明。

成功后输出以下文件：

| 文件 | 内容 |
| --- | --- |
| `XmaxSDK-<version>-windows-x64.zip` | 原生 DLL、Agora 依赖、头文件、导入库、CMake 配置、符号、说明和消费样例 |
| `XLab-<version>-windows-x64.zip` | Qt Demo 与运行依赖 |
| `Xmax.SDK.<version>.nupkg` | 通用 C# SDK 与原生运行库 |
| `Xmax.SDK.Wpf.<version>.nupkg` | WPF 视频控件 |
| `Xmax.SDK.WinUI.<version>.nupkg` | WinUI 3 视频控件 |
| `XLabWpf-<version>-win-x64.zip` | WPF Demo 与运行依赖 |
| `XLabWinUI-<version>-win-x64.zip` | WinUI 3 Demo 与运行依赖 |
| `SHA256SUMS.txt` | 上述七个包的校验值 |
| `validation.json` | 对应版本、源码提交及本地验证记录 |

原生和 Qt ZIP 另含逐文件 manifest。发布检查会再次检查包的校验值、NuGet 标识和版本、托管包与 Demo 内的原生 DLL 一致性，以及原生依赖说明。包缺失或被修改时拒绝上传。

`--native-only` 可仅构建原生和 Qt 包，供局部验证；`--without-rtc` 可验证无 RTC 的构建。这两种结果都不能作为完整发布包。

## 发布演练

在有完整产物的机器上执行：

```sh
release_version="$(cat VERSION)"
./.cicd/cd-github-release.sh "$release_version" --artifacts-dir .build/artifacts
```

演练仅检查本地提交、版本、Release Notes 和产物，不要求已推送 main 或创建标签，也不访问 GitHub。Windows 使用 `.cicd/cd-github-release.ps1`，通过 `$releaseVersion = (Get-Content VERSION -Raw).Trim()` 读取版本。

## 上传代码与发布包

先将已验证提交快进合入 main，确认 main 指向产物记录的提交，再推送代码和版本标签：

```sh
git switch main
release_version="$(cat VERSION)"
git push origin main
git tag -a "$release_version" -m "release: XmaxSDK Windows $release_version"
git push origin "$release_version"
./.cicd/cd-github-release.sh "$release_version" --artifacts-dir .build/artifacts --publish
```

上传需要 Git 和已登录的 GitHub CLI（`gh`）。只有 `--publish` 会创建 Release 并上传附件；它核对远端 main、不可变版本标签和包的源码提交，并拒绝覆盖已有 Release。脚本不会自动推送代码、创建标签或发布到 NuGet。网络中断后先检查远端状态，再决定是否重试。

已有 feature/develop/main 分支协作时，可继续使用 `ci-merge` 做快进集成；它只运行本地验证，不查询 GitHub 检查状态。普通代码同步直接使用 Git 即可，发布不依赖该入口。

真实摄像头、音频、云端生成、COS 及持续运行验收需在目标环境单独完成；媒体能力和接口限制统一见[使用说明](../Materials/usage.md)。构建报告记录实际验证结果，文档不维护固定测试数量或历史性能快照。
