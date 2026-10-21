# Windows CI/CD 与发版

远端为 `Xmax-AI/XmaxSDK-Windows`。三个流程入口对应 macOS 工程的职责：
`ci-prepare` 准备版本、`ci-merge` 验证并集成、`cd-github-release` 构建产物后检查并发布。
Shell 与 PowerShell 包装都调用同一个 Python 实现，避免两套逻辑漂移。

本目录随源码提交，GitHub Actions 调用其中的构建、验包与发布检查。
`Docs/` 仍忽略，公开说明在 Materials。构建缓存、产物和 .NET bin/obj 均不提交。

## 分支与版本

- `feature/<description>`：日常修改；`develop`：集成基线；`main`：发布基线。
- `VERSION` 是版本号唯一构建来源，CMake、运行时版本和 Windows DLL 资源由它生成。
- SDK 标签采用 `0.2.0` 形式，不带 `v`；当前只接受稳定的 major.minor.patch。
- 所有 SDK 标签保持不可变。流程拒绝覆盖已存在的 GitHub Release。

空仓库首次推送 main 后，可从相同基线建立 develop。后续在 feature 分支开发。

## 日常 CI

`.github/workflows/build.yml` 在 main/develop/feature/fix 推送、PR 和手动触发时运行：

1. Source checks：版本、ABI 基线、依赖 SHA、文档链接、ignore 规则、隔离流程测试、actionlint。
2. macOS SDK：公共 C++ 与 C ABI 测试，安装后独立 C/C++ 消费端。
3. macOS sanitizers：ASan / UBSan 覆盖全部核心测试。
4. Windows SDK and package：真正的 Windows 2022 / VS2022 x64 构建，无 RTC 构建先验证核心独立性，再构建 Agora + Qt、打包并解压消费。

Windows 发布构建包含 DLL 版本资源和 PDB。SDK DLL 导出必须与公开头一致，并保留 ABI v1 基线；禁止依赖 Qt 和 Debug CRT。

Qt 6.8.3、aqtinstall、Agora 4.6.2 的下载源和 SHA 固定在 dependencies.json；官方 Actions 固定到经核对的提交。普通 CI 只使用读取权限。

本地验证：

```sh
python3 .cicd/check-source.py
python3 .cicd/test-workflow.py
python3 .cicd/pipeline.py verify --build-dir .build/ci
python3 .cicd/pipeline.py verify --sanitizers --build-dir .build/ci-sanitized
```

Windows 完整打包，需 VS2022 C++ 工具、Windows SDK、Python 3.10+、CMake、Qt 和 .NET 8 SDK：

```powershell
python .cicd/pipeline.py build-windows --qt-prefix C:/Qt/6.8.3/msvc2022_64
# 或 ./.cicd/build-windows.ps1 --qt-prefix C:/Qt/6.8.3/msvc2022_64
```

没有指定 Agora 根目录时，调用 scripts/get-agora.ps1，校验原始 ZIP 后展开。产物默认位于 .build/artifacts。

## 交付与验包

- `XmaxSDK-<version>-windows-x64.zip`：DLL、Agora DLL、import lib、公开头、CMake package、符号、许可证、安装说明与 C/C++/.NET 消费样例。
- `XLab-<version>-windows-x64.zip`：SDK、Qt 运行库/QML/插件、Demo 和依赖说明。
- `SHA256SUMS.txt`：两个 ZIP 的校验值。
- `validation.json`：版本、提交、Windows 构建与消费端验证记录。
- 每个 ZIP 内另有 manifest.json，逐文件记录 SHA-256 与源码提交。

构建流程从全新 staging 安装，再将 SDK ZIP 解压到新目录，对解压后的 DLL 运行版本、能力和导出检查，构建 C/C++ 与 .NET P/Invoke 消费端。Demo 部署后及再次解压后都执行 UI smoke test 和真实模式的空 Key 拦截测试，并检查 WebP 解码插件存在。Qt 行为测试使用本地 PNG/WebP 验证解码、圆形绘制、缓存和失败重试，不依赖在线图片服务。测试的用户设置隔离在临时目录，不读取或改写真实 API Key。所有步骤成功才写 validation.json。

已验证候选包通过 Actions Artifact 下载，保留 14 天；这不是 GitHub Release。

## 准备版本

在干净的 feature 分支：

```sh
./.cicd/ci-prepare.sh 0.3.0 --dry-run
./.cicd/ci-prepare.sh 0.3.0
```

PowerShell 使用同名 .ps1。准备过程检查本地/远端标签不存在，更新 VERSION 和公开文档中的当前版本，为新版本生成 release-notes 模板；不会 commit、push 或打 tag。填好 What's New、API Changes 和 Validation 后提交，模板中的占位符会阻止正式发布。

## 集成

提交并推送 feature，等远端 CI 全部通过：

```sh
./.cicd/ci-merge.sh --dry-run
./.cicd/ci-merge.sh
```

检查工作区干净、feature 已推送、main/develop 可以快进、本地主分支与远端一致，运行本地验证并核对相同提交的远端必需检查。成功后原子快进远端 develop/main，再同步本地引用；失败不推送主分支。

`--skip-validation` 只供明确跳过验证或隔离测试，保留 Git 状态与快进检查，不代表构建已通过。普通集成不使用此选项。

## 演练 CD（不发布）

在 GitHub Actions 手动执行 `SDK Release`，填写 VERSION 中的版本号，保持 publish=false。
它会重建并验证完整 Windows 候选包，再在独立任务重新解压检查提交、DLL 导出和 SHA-256，只上传 Actions Artifact：不创建 tag，不创建 Release，不发布下载附件。

```sh
gh workflow run release.yml --repo Xmax-AI/XmaxSDK-Windows --ref main -f version=0.2.0 -f publish=false
```

## 正式发布（本次未执行）

先完成 Windows 实机与云端验收，确认 main 与预期提交一致，再手动创建、推送对应的不可变标签：

```sh
git switch main
git tag -a 0.2.0 -m "release: XmaxSDK Windows 0.2.0"
git push origin 0.2.0
```

标签推送本身不会触发发布。随后手动运行 SDK Release，并显式选择 publish=true。
工作流重新核对远端 main、标签、VERSION、完整 Notes、同一提交的候选包和每个文件的校验值；只有发布 job 获得 contents:write 权限。

已有完整 Windows 产物时，也可在干净 main 上先执行本地演练，再明确发布：

```sh
./.cicd/cd-github-release.sh 0.2.0 --artifacts-dir .build/artifacts
./.cicd/cd-github-release.sh 0.2.0 --artifacts-dir .build/artifacts --publish
```

本地 release 演练也要求正确 main、远端标签与完整产物，唯一区别是没有 gh release create。
发布命令使用 --verify-tag，并拒绝覆盖已有 Release。网络失败后先查看远端状态再重试；不会自动删除或覆盖附件。发版成功后下一开发分支由维护者创建。

## 能力验收边界

自动化能验证编译、链接、ABI、安装和无凭证流程；不能替代真实摄像头、音频、云端生成、COS 或持续运行性能验收。插帧、D3D11 纹理零拷贝、外部音频和轨迹 API 当前未实现，不能据候选包通过而宣称与 macOS 全功能等价。
