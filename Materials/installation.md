# Windows 安装说明

所有命令均在工程根目录 `Windows/XmaxSDK` 执行。

## 接入 Windows 二进制包

候选包由 GitHub Actions 的 Windows 构建生成；正式 Release 通过验收后再发布。
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
# .NET 8 SDK：
dotnet run --project samples/DotNetSmoke --configuration Release "-p:XmaxNativeDir=$PWD/bin"
```

## 构建 SDK

需要 CMake 3.24+、C++17 编译器和平台 SDK。Windows 使用 Visual Studio 2022 的 C++ 桌面开发工具及 Windows SDK；便携开发预设使用 Ninja。

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
$agora = ./scripts/get-agora.ps1
$qt = "C:/Qt/6.8.3/msvc2022_64"
cmake --preset windows -DXMAX_BUILD_EXAMPLE=ON -DXMAX_WITH_AGORA=ON "-DXMAX_AGORA_ROOT=$agora" "-DCMAKE_PREFIX_PATH=$qt"
cmake --build --preset windows
& "$qt/bin/windeployqt.exe" --release --qmldir Examples/XLab/qml .build/windows/Examples/XLab/Release/XLab.exe
& .build/windows/Examples/XLab/Release/XLab.exe
```

下载脚本固定 Agora Windows 4.6.2，并校验 SHA-256。构建复制 SDK 与 Agora DLL；Qt 运行库、QML、多媒体和图片插件由 `windeployqt` 部署。发布时保留 [依赖许可证及说明](../THIRD_PARTY.md)。

## 接入安装包

安装目录包含 `include/xmax`、Windows `bin/XmaxSDK.dll` 与 `lib/XmaxSDK.lib`；启用 Agora 时还会安装其 DLL。CMake 消费端：

```cmake
find_package(XmaxSDK CONFIG REQUIRED)
add_executable(MyApp main.cpp)
target_compile_features(MyApp PRIVATE cxx_std_17)
target_link_libraries(MyApp PRIVATE Xmax::SDK)
```

配置消费端时，将安装前缀传入 `CMAKE_PREFIX_PATH`。运行时确保 SDK 与 Agora DLL 位于应用可加载的位置。C API 使用 `include/xmax/xmax.h`；C++ RAII 包装使用 `include/xmax/xmax.hpp`。直接使用 C API 的宿主无需 Qt。

## Mac 示例预览

```sh
cmake --preset xlab -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos
cmake --build --preset xlab
bash scripts/run-xlab.sh --preview
# 直接预览会话页面
bash scripts/run-xlab.sh --preview-session
```

当前工作区复用已安装的本地工具和 Qt，也可运行 `bash scripts/verify.sh --example`。这些依赖缓存位于忽略的 `.build`，源码分发不包含缓存。

Mac 构建提供 AVFoundation 摄像头实现，可开发公共 C++ 逻辑和 Qt UI；当前没有集成 macOS Agora C++ 后端。默认测试使用采集和传输替身，不打开设备或使用真实凭证。

当前还通过 Windows x64 GNU 目标交叉构建生成了关闭 Agora 的 DLL，并检查了 C 导出；Agora 4.6.2 适配器已用真实 Windows 头编译为对象文件。正式发布仍应使用 MSVC 构建并在 Windows 验证原生设备、WinHTTP、Agora 连接和生成，交叉编译不代替这些验收。

继续阅读 [使用说明](usage.md)。

发版准备、Windows 验包与不发布的 CD 演练见 [CI/CD 说明](../.cicd/README.md)。
