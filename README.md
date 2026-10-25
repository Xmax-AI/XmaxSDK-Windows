<p align="center">
  <img src="./Materials/images/brand/xmax-sdk.png" alt="XmaxSDK — Realtime Interactive Video Generation" width="880">
</p>

<p align="center">
  <a href="https://learn.microsoft.com/windows/"><img src="https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-007AFF" alt="Windows 10 / 11 x64"></a>
  <a href="https://isocpp.org/"><img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17"></a>
  <a href="https://dotnet.microsoft.com/"><img src="https://img.shields.io/badge/.NET-10-512BD4" alt=".NET 10"></a>
  <a href="https://platform.xmaxai.com/"><img src="https://img.shields.io/badge/Realtime-AI-FF9500" alt="Realtime AI"></a>
  <a href="./LICENSE"><img src="https://img.shields.io/badge/License-MIT-4C9A2A" alt="MIT License"></a>
</p>

We introduce XmaxSDK, a native Windows SDK designed for real-time interactive video generation via Xmax models. XmaxSDK implements an end-to-end pipeline covering media acquisition, video streaming, frame-by-frame generation, and on-device rendering, enabling developers to seamlessly integrate low-latency, high-fidelity video transformations into creative applications at a much lower cost than alternative solutions.

<p align="center"><img src="./Materials/images/xlab/generation-demo.gif" alt="X-Lab realtime generation demo" width="33%" /><img src="./Materials/images/xlab/index-demo.gif" alt="X-Lab index demo" width="33%" /><img src="./Materials/images/xlab/storage-demo.gif" alt="X-Lab storage demo" width="33%" /></p>
<p align="center"><sub>iOS X-Lab demos</sub></p>

<br>

## What you can build with XmaxSDK

<table>
  <tr>
    <th width="24%" align="left">Realtime Use Case</th>
    <th width="60%" align="left">Description</th>
    <th width="16%" align="center">Demo</th>
  </tr>
  <tr>
    <td rowspan="2" width="24%" valign="middle">
      <strong>Character Swapping</strong>
    </td>
    <td width="60%" valign="middle">
      Replace anyone in your live feed with a designated avatar in real-time.
    </td>
    <td rowspan="2" width="16%" align="center" valign="middle">
      <a href="./Materials/videos/use-cases/character-swapping.mp4">
        <img src="./Materials/images/use-cases/character-swapping-poster.png" alt="Play the Character Swapping demo" width="120">
        <br>
        <sub>▶ Play demo</sub>
      </a>
    </td>
  </tr>
  <tr>
    <td width="60%" valign="middle">
      <strong>Prompt:</strong> <code>视频中角色替换成参考图中角色</code>
      <br><br>
      <strong>Reference image:</strong> Select a clear image of the desired character with a clean background.
    </td>
  </tr>
  <tr>
    <td rowspan="2" width="24%" valign="middle">
      <strong>Virtual Try-On</strong>
    </td>
    <td width="60%" valign="middle">
      Seamlessly change outfits, preserving exact body shape, natural motion, and an
      authentic fit.
    </td>
    <td rowspan="2" width="16%" align="center" valign="middle">
      <a href="./Materials/videos/use-cases/virtual-try-on.mp4">
        <img src="./Materials/images/use-cases/virtual-try-on-poster.png" alt="Play the Virtual Try-On demo" width="120">
        <br>
        <sub>▶ Play demo</sub>
      </a>
    </td>
  </tr>
  <tr>
    <td width="60%" valign="middle">
      <strong>Prompt:</strong> <code>视频中人物衣服替换成参考图中衣服</code>
      <br><br>
      <strong>Reference image:</strong> Select a clear image of the target outfit with a clean background.
    </td>
  </tr>
  <tr>
    <td rowspan="2" width="24%" valign="middle">
      <strong>Video Restyling</strong>
    </td>
    <td width="60%" valign="middle">
      Reimagine your world in any style with an immersive visual experience.
    </td>
    <td rowspan="2" width="16%" align="center" valign="middle">
      <a href="./Materials/videos/use-cases/video-restyling.mp4">
        <img src="./Materials/images/use-cases/video-restyling-poster.png" alt="Play the Video Restyling demo" width="120">
        <br>
        <sub>▶ Play demo</sub>
      </a>
    </td>
  </tr>
  <tr>
    <td width="60%" valign="middle">
      <strong>Prompt:</strong> <code>视频风格变为参考图指定的风格</code>
      <br><br>
      <strong>Reference image:</strong> Select an image that captures the artistic style you want to apply.
    </td>
  </tr>
  <tr>
    <td rowspan="2" width="24%" valign="middle">
      <strong>AI Companions</strong>
    </td>
    <td width="60%" valign="middle">
      Summon virtual characters into your live camera feed and interact with them
      through gestures.
    </td>
    <td rowspan="2" width="16%" align="center" valign="middle">
      <a href="./Materials/videos/use-cases/ai-companions.mp4">
        <img src="./Materials/images/use-cases/ai-companions-poster.png" alt="Play the AI Companions demo" width="120">
        <br>
        <sub>▶ Play demo</sub>
      </a>
    </td>
  </tr>
  <tr>
    <td width="60%" valign="middle">
      <strong>Prompt:</strong> <code>指定角色在场景中互动</code>
      <br><br>
      <strong>Reference image:</strong> Select a clear image of the virtual character you want to summon with a clean background.
    </td>
  </tr>
</table>

<br>

## Why XmaxSDK?

<table>
  <thead>
    <tr>
      <th height="104" align="center" valign="middle">
        <img src="./Materials/images/why/low-latency.svg" alt="Low latency" width="36" height="36"><br>Low latency
      </th>
      <th height="104" align="center" valign="middle">
        <img src="./Materials/images/why/low-cost.svg" alt="Cost efficiency" width="36" height="36"><br>Cost efficiency
      </th>
      <th height="104" align="center" valign="middle">
        <img src="./Materials/images/why/high-fidelity.svg" alt="High fidelity" width="36" height="36"><br>High fidelity
      </th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>End-to-end latency is measured in <img src="./Materials/images/why/latency-highlight.svg" alt="hundreds of milliseconds" width="192" height="20" align="absmiddle">, ensuring that updates to generation conditions and interaction controls are reflected instantly.</td>
      <td>Run on a <img src="./Materials/images/why/gpu-highlight.svg" alt="single RTX 5090" width="126" height="20" align="absmiddle">, reducing inference costs by orders of magnitude versus datacenter GPUs like H100.</td>
      <td>Our models support real-time generation at up to <img src="./Materials/images/why/resolution-highlight.svg" alt="1080p" width="48" height="20" align="absmiddle">, delivering production-ready, high-quality video output.</td>
    </tr>
  </tbody>
</table>

<br>

## Prerequisites

- Windows 10 version 2004 (19041) or later, or Windows 11; x64 applications
- A C++17 compiler, Visual Studio 2022 C++ desktop tools and CMake 3.24 or later for native source builds
- .NET 10 SDK for C# applications; Windows App SDK for WinUI 3 applications
- An Xmax API key

> [!WARNING]
> Never commit your Xmax API key to version control. Pass it securely at
> runtime or use short-lived temporary keys issued by the Xmax API. For step-by-step
> instructions, see [Authentication](https://platform.xmaxai.com/docs/authentication).

<br>

## Installation

XmaxSDK supports [**CMake**](#cmake), [**NuGet**](#nuget),
and [**manual integration**](#manual).

当前开发版本为 `1.0.0`，C ABI 版本为 `1`。

### CMake

Extract the Windows SDK package and set `CMAKE_PREFIX_PATH` to its root directory.
Add the **Xmax::SDK** target to your application:

```cmake
find_package(XmaxSDK CONFIG REQUIRED)
add_executable(YourApp main.cpp)
target_compile_features(YourApp PRIVATE cxx_std_17)
target_link_libraries(YourApp PRIVATE Xmax::SDK)
```

The public C API is available through `<xmax/xmax.h>`; C++ applications can use
`<xmax/xmax.hpp>` for the RAII wrappers. See the
[installation guide](./Materials/installation.md) for source builds and runtime deployment.

### NuGet

Use the packages produced by the [local packaging workflow](./.cicd/README.md#本地验证与打包)
as a local NuGet source, then add **Xmax.SDK** to your C# application:

```powershell
dotnet add package Xmax.SDK --source C:/path/to/packages
```

For WinUI 3 or WPF video controls, add the corresponding package:

```powershell
dotnet add package Xmax.SDK.WinUI --source C:/path/to/packages
# Or, for WPF:
dotnet add package Xmax.SDK.Wpf --source C:/path/to/packages
```

Target an x64 process. The UI packages reference the common C# SDK; the native SDK
runs independently of WinUI, WPF and Qt. See the [C# integration guide](./Materials/dotnet.md)
for project configuration.

### Manual

Download the Windows SDK archive from
[GitHub Releases](https://github.com/Xmax-AI/XmaxSDK-Windows/releases),
or build it with the [local packaging workflow](./.cicd/README.md#本地验证与打包).
Follow the [manual integration guide](./Materials/installation.md#接入-windows-二进制包)
to add the headers, import library, native DLLs and required runtime dependencies
to your application.

<br>

## Quick Start

### Configure permissions

Enable camera access and desktop application camera access in Windows Settings.
Enable microphone access when using the microphone. Packaged applications must
also declare the capabilities used by the application in `Package.appxmanifest`:

```xml
<Capabilities>
  <DeviceCapability Name="webcam" />
  <DeviceCapability Name="microphone" />
</Capabilities>
```

XmaxSDK uses Media Foundation for camera capture and reports an `XmaxException`
when native operations fail. Unpackaged desktop applications use the system's
desktop application privacy settings. See the
[installation guide](./Materials/installation.md) for media components and runtime setup.

<br>

### Generate and display video

The following WinUI 3 snippet creates a camera stream, starts real-time generation,
and binds the output to a video view. Run this within an async context on the UI thread.
Keep the client and realtime controller alive while the generation screen is in use.

```csharp
using Xmax.SDK;
using Xmax.SDK.WinUI;

var client = new XmaxClient(new XmaxConfiguration
{
    ApiKey = "YOUR_XMAX_API_KEY"
});

var realtime = client.CreateRealtime(new RealtimeConfiguration
{
    Model = "x2.1"
});

await realtime.PrepareCameraAsync(
    format: XmaxClient.GetModelInfo().DefaultCameraFormat
);

var localVideoView = new XmaxRealtimeVideoView
{
    VideoSource = realtime.LocalVideo,
    ContentMode = VideoContentMode.Fill
};
```

Add the video view to your window. `PrepareCameraAsync` completes after the first
camera frame is available and the input is `Ready`; then start generation:

```csharp
await realtime.StartAsync(new GenerationContext(
    "视频中角色替换成参考图中角色",
    "https://platform.xmaxai.com/images/source/charx/chatx_image1.jpg"
));

var remoteVideoView = new XmaxRealtimeVideoView
{
    VideoSource = realtime.RemoteVideo,
    ContentMode = VideoContentMode.Fill
};
```

Add the result view to your window alongside the local preview.
`XmaxRealtimeVideoView` displays one local or remote video source, applies frame
mirror metadata, and supports aspect-fit and aspect-fill layouts. The application
owns the layout and any transition between local preview and generated results.
See the [WinUI 3 guide](./Materials/winui.md) for XAML binding and the XLab demo.

<br>

### Using WPF

Use `XmaxRealtimeVideoView` from **Xmax.SDK.Wpf** in your WPF application. Bind its
`VideoSource` to `realtime.LocalVideo` or `realtime.RemoteVideo`:

```xml
<sdk:XmaxRealtimeVideoView
    xmlns:sdk="clr-namespace:Xmax.SDK.Wpf;assembly=Xmax.SDK.Wpf"
    VideoSource="{Binding RemoteVideo}"
    ContentMode="Fill" />
```

See the [WPF guide](./Materials/wpf.md) for project setup and state binding.
Both WPF and WinUI 3 use the same C# realtime controller and video source interfaces.

<br>

### Pointer trajectory interaction

Once generation is confirmed and a result frame is ready, collect pointer samples
from the remote video viewport and submit them to the SDK. The application owns
pointer event handling and sampling; the SDK maps coordinates and sends interaction
trajectories to the current generation task. Local previews do not send trajectories.

```csharp
void SendPointerSample(double x, double y)
{
    realtime.SubmitInteraction(
        [new TrajectoryPoint(x, y)],
        remoteVideoView.ActualWidth,
        remoteVideoView.ActualHeight,
        remoteVideoView.VideoWidth,
        remoteVideoView.VideoHeight,
        remoteVideoView.ContentMode
    );
}
```

Use the same units for pointer positions and viewport dimensions. Start sampling
on a primary-button drag and stop on release, Escape, focus loss, resizing, source
changes or generation shutdown. Use `GetTrajectory()` to obtain normalized points
for a custom display overlay. Native HWND applications can use the SDK's
`render_window` helper to draw video with the default trajectory effect.

Trajectory coordinates follow the displayed video: the left edge is `x = 0` and the
right edge is `x = 1`, regardless of display mirroring. Fit-mode black bars ignore clicks. Dragging beyond the viewport clamps to the
visible video edge. Display overlays are not included in media frame callbacks.
See the [interaction guide](./Materials/usage.md#轨迹交互) for coordinate mapping and native APIs.

### Listen for events

After creating `realtime`, register the listeners you need before preparing the
input stream or starting generation.

| Listener | Purpose |
| --- | --- |
| `EventReceived` (`State`, `Error`) | Observe pipeline states and termination reasons during real-time generation. |
| `SetRemoteMediaListenerAsync` | Receive remote PCM and processed video with shared stream identity and timestamps. |
| `EventReceived` (`Statistics`, `Quality`) | Monitor uplink and downlink network quality. |
| `EventReceived` (`Performance`) | Detect device performance limitations or recovery. |

For example, monitor state changes and errors:

```csharp
realtime.EventReceived += e =>
{
    if (e.Type == RealtimeEventType.State)
        Console.WriteLine($"State: {e.State}");
    if (e.Type == RealtimeEventType.Error || e.Reason == 2)
        Console.WriteLine($"Error: {e.Code} {e.Message}");
};
```

Handle errors thrown by async calls with `try/catch`. State snapshots from
`GetStateAsync()` include the state reason and associated failure details.
Events are delivered serially on a background thread; dispatch UI changes through
your framework's dispatcher. Dispose or transfer ownership of every media packet
received through `SetRemoteMediaListenerAsync`.

For camera input, bind `realtime.LocalVideo` to a preview view. The SDK enters
`Ready` after native camera capture produces its first frame; observe this through
`EventReceived` or await `PrepareCameraAsync`. The Windows SDK delivers local
preview frames through its latest-frame mailbox.

<br>

### Resource Cleanup

- **`DisconnectAsync()` — Stop Remote Generation**

  Stops remote generation and cancels billing while keeping the local camera stream
  and preview active. Use this when ending the online session but staying on the
  current screen. You can start a new session later using the same prepared media:

  ```csharp
  await realtime.DisconnectAsync();
  ```

- **`CloseAsync()` — Full Teardown & Release**

  Ends the remote session, stops local media capture, and clears generation
  conditions. Use this when leaving or dismissing the generation screen:

  ```csharp
  await realtime.CloseAsync();
  ```

> **Note:** These methods are alternatives, not sequential steps. When exiting a
> screen, call `CloseAsync()` directly—there is no need to call `DisconnectAsync()` first.

When the controller is no longer needed, call `await realtime.DisposeAsync()` to
release its native handle and event worker, then `client.Dispose()`. An `await using`
scope can manage the controller's lifetime and perform the full teardown directly.

<br>

> [!TIP]
> For complete usage examples, including camera input, reference images,
> and external video input, see the [usage guide](./Materials/usage.md).

<br>

## Dependencies

- <ins><strong>Agora RTC SDK for Windows</strong></ins> enables low-latency, real-time audio and video communication.
- <ins><strong>Tencent Cloud COS integration</strong></ins> handles media upload and download via object storage, using SDK-managed HTTPS requests and HMAC signing through WinHTTP and BCrypt.

<br>

## Contact us

For bug reports and feature requests, please open a
[GitHub Issue](https://github.com/Xmax-AI/XmaxSDK-Windows/issues). For integration
assistance and technical support, contact us at [sdk@xmax.ai](mailto:sdk@xmax.ai).

<br>

## License

XmaxSDK is available under the terms of the [MIT License](LICENSE).
