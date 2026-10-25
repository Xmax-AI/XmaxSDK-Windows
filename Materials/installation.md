# Windows 安装说明

所有命令均在工程根目录 `Windows/XmaxSDK` 执行。

## 接入 Windows 二进制包

二进制发布包见 [GitHub Releases](https://github.com/Xmax-AI/XmaxSDK-Windows/releases)，本地构建与打包方式见本文。
SDK 包包含 `bin/XmaxSDK.dll`、Agora DLL、`lib/XmaxSDK.lib`、`include/xmax`、CMake package、符号与消费样例。
Qt 仅在独立 XLab 包中，SDK 包不需要 Qt。

1. 校验 SHA256SUMS，解压整个 SDK 包。
2. 安装 Microsoft Visual C++ v14 x64 Redistributable；运行时将 bin 中所需 DLL 放在应用可加载位置。
3. C/C++ 工程使用下文的 find_package；将解压后的根目录设为 CMAKE_PREFIX_PATH。
4. 可构建包内 samples/consumer 验证 C/C++，或使用 samples/DotNetSmoke 验证 P/Invoke。
5. Windows 摄像头依赖 Media Foundation 和系统摄像头权限；N/KN 或未安装媒体功能的系统需先安装对应媒体组件。

```powershell
cmake -S samples/consumer -B consumer-build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$PWD"
cmake --build consumer-build --config Release
ctest --test-dir consumer-build -C Release --output-on-failure
# .NET 10 SDK：
dotnet run --project samples/DotNetSmoke --configuration Release "-p:XmaxNativeDir=$PWD/bin"
```

CMake 消费端：

```cmake
find_package(XmaxSDK CONFIG REQUIRED)
add_executable(MyApp main.cpp)
target_compile_features(MyApp PRIVATE cxx_std_17)
target_link_libraries(MyApp PRIVATE Xmax::SDK)
```

配置消费端时，将安装前缀传入 `CMAKE_PREFIX_PATH`。运行时确保 SDK 与 Agora DLL 位于应用可加载的位置。C API 使用 `include/xmax/xmax.h`；C++ RAII 包装使用 `include/xmax/xmax.hpp`。直接使用 C API 的宿主无需 Qt。

## 构建 SDK

需要 CMake 3.24+、C++17 编译器和平台 SDK。Windows 使用 Visual Studio 2022 的 C++ 桌面开发工具及 Windows SDK；便携开发预设使用 Ninja。

首次 CMake 配置会按 [依赖锁定文件](../Configurations/Release/dependencies.json)下载 JSON 头文件和许可证，逐项校验 SHA-256，并缓存在构建目录的 `dependencies/`。可用 `-DXMAX_DEPENDENCIES_DIR=<cache-directory>` 指定已有缓存；缓存内容校验通过后无需重新下载。

Windows x64：

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
cmake --install .build/windows --config Release --prefix .build/install
```

Mac 公共逻辑构建与测试：

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
cmake --install .build/core --prefix .build/install
```

默认关闭 Agora，适用于本地构建、接口和生命周期测试。此时 `xmax_client_has_rtc` 返回未提供 RTC 后端，开始生成返回 `XMAX_UNSUPPORTED`，不会先创建服务端 Session。

## Windows Agora 与 XLab

安装 Qt 6.8+ 的 MSVC x64 版本，包含 Qt Quick、Quick Controls、Multimedia、Image Formats。Qt 仅供示例使用，SDK 本身不依赖 Qt。

```powershell
$locked = Get-Content Configurations/Release/dependencies.json -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force .build/vendor/agora | Out-Null
$archive = "$PWD/.build/vendor/agora.zip"
Invoke-WebRequest $locked.agora.url -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $locked.agora.sha256) {
    throw 'Agora archive SHA-256 mismatch.'
}
Expand-Archive $archive -DestinationPath .build/vendor/agora -Force
$agora = "$PWD/.build/vendor/agora/Agora_Native_SDK_for_Windows_FULL/sdk"
$qt = "C:/Qt/6.8.3/msvc2022_64"
cmake --preset windows -DXMAX_BUILD_EXAMPLE=ON -DXMAX_WITH_AGORA=ON "-DXMAX_AGORA_ROOT=$agora" "-DCMAKE_PREFIX_PATH=$qt"
cmake --build --preset windows
& "$qt/bin/windeployqt.exe" --release --qmldir Examples/XLab/qml .build/windows/Examples/XLab/Release/XLab.exe
& .build/windows/Examples/XLab/Release/XLab.exe
```

上述命令根据 [依赖锁定文件](../Configurations/Release/dependencies.json)选择 Agora 版本并校验 SHA-256。构建复制 SDK 与 Agora DLL；Qt 运行库、QML、多媒体和图片插件由 `windeployqt` 部署。发布时保留 [依赖许可证及说明](dependencies.md)。

## Mac 与 Parallels Windows 共用源码

本地协作采用一份工作区：Mac 保存 Git 仓库，Parallels 仅共享该工程目录为 `XmaxSDK`，Windows 通过 `\\Mac\XmaxSDK` 直接读写。开发脚本复用 Parallels 的现有盘符映射（示例使用 `S:`），没有映射时选择空闲盘符，兼容 Qt 构建命令对当前目录的要求。任一侧保存的源码改动都会在另一侧可见，不需要复制、提交或推送后才能测试。Git 提交、切分支和推送统一在 Mac 执行。

构建目录和依赖放在 Windows 本地磁盘，避免混用两个平台的缓存：

| 内容 | 路径 |
| --- | --- |
| Mac 主源码 | 当前 `Windows/XmaxSDK` 工作区 |
| Windows 中的同一份源码 | `S:\`，对应 `\\Mac\XmaxSDK`（盘符由本机映射决定） |
| Windows 构建产物、解决方案和 PDB | `C:\dev\XmaxSDK-build` |
| Windows Agora 依赖 | `C:\dev\XmaxSDK-deps\agora\sdk` |

在 Windows PowerShell 中直接构建共享源码，输出放到本地磁盘：

```powershell
cmake -S S:/ -B C:/dev/XmaxSDK-build -G "Visual Studio 17 2022" -A x64 `
  -DXMAX_WITH_AGORA=ON -DXMAX_BUILD_EXAMPLE=ON `
  -DXMAX_AGORA_ROOT=C:/dev/XmaxSDK-deps/agora/sdk `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build C:/dev/XmaxSDK-build --config RelWithDebInfo --parallel 4
ctest --test-dir C:/dev/XmaxSDK-build -C RelWithDebInfo --output-on-failure
```

Visual Studio 可打开 `C:/dev/XmaxSDK-build/XmaxWindows.sln`，实时媒体验证选择 `RelWithDebInfo | x64`，保留优化和 PDB 调试符号。开发者也可使用自行保留的本地工具，源码构建不依赖这些工具。重新编译前关闭对应 Demo，以释放 DLL 文件。

媒体性能可单独测量，不依赖 API Key 或云端服务。在 Windows 构建完成后，显式指定采样秒数与唯一匹配的摄像头名称片段：

```powershell
& 'C:\dev\XmaxSDK-build\Sources\XmaxSDK\RelWithDebInfo\xmax_media_benchmark.exe' 5 --camera 'MacBook Pro'
```

基准分别测量原始采集和 SDK 直通链路的实际尺寸、帧数与帧率；采样前等待首帧，排除设备启动耗时。采样时长为 1 至 30 秒；未指定 `--camera` 时仅显示用法，不打开设备。结果不代表公网发送帧率或云端生成帧率，也不作为 CTest 的速度门槛。

源码共享不能代替 Git 版本管理。不同分支和平台应使用独立的构建目录。

## Mac 示例预览

```sh
cmake --preset xlab -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos
cmake --build --preset xlab
# 正常模式：空 Key 会打开输入框；当前 Mac 构建没有 RTC 后端，会明确提示。
.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab
# 仅预览 UI，不校验 Key、不开摄像头、不连接生成服务：
.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab --preview
# 直接预览会话页面
.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab --preview-session
```

构建完成后使用 `ctest --preset core` 或 `ctest --test-dir .build/xlab --output-on-failure` 验证。构建缓存位于忽略的 `.build`，源码分发不包含缓存。

Mac 构建提供 AVFoundation 摄像头实现，可开发公共 C++ 逻辑和 Qt UI；当前没有集成 macOS Agora C++ 后端。默认测试使用采集和传输替身，不打开设备或使用真实凭证。

`--preview` / `--preview-session` 的标题、启动按钮与视频面板均标注 UI Preview。预览不会输出摄像头画面或生成结果。
在线预设缩略图支持本地缓存，加载失败显示警告，可悬停查看原因并点击重试；图片域名无法访问时不能显示实际缩略图。

平台能力和输入限制见 [使用说明](usage.md)。真实摄像头、音频、云端生成及持续性能需要在目标 Windows 环境验收；UI 预览不替代实时会话验收。

## 本地打包

原生 SDK 可从 CMake 构建目录安装到独立目录：

```powershell
cmake --install .build/windows --config Release --prefix .build/install
```

需要包含依赖说明、校验值和验证报告的正式发行包时，从 [GitHub Releases](https://github.com/Xmax-AI/XmaxSDK-Windows/releases) 下载。

C#、WPF 和 WinUI 可直接使用 .NET CLI 打包。原生 DLL 与依赖说明来自已构建安装或解压的 SDK：

```powershell
dotnet pack Sources/XmaxSDK.Net -c Release -o C:/dev/XmaxSDK-packages `
  -p:XmaxNativeDir=C:/path/to/sdk/bin -p:XmaxNativeNoticesDir=C:/path/to/sdk/licenses
dotnet pack Sources/XmaxSDK.Wpf -c Release -o C:/dev/XmaxSDK-packages
dotnet pack Sources/XmaxSDK.WinUI -c Release -p:Platform=x64 -o C:/dev/XmaxSDK-packages
```

共享源码环境还需添加 `-p:XmaxDotNetBuildRoot=C:/dev/XmaxSDK-dotnet/`，将中间文件写入 Windows 本地磁盘。使用 `dotnet publish` 发布对应的 Demo 项目，并保留原生与托管依赖的许可证。上述命令生成本地文件，不会自动上传。

依赖锁定及 ABI 基线位于 [`Configurations/Release`](../Configurations/Release)。发布维护工具与 Release Notes 仅保留在维护者本地，不是源码构建或 SDK 接入的前提。
