//
//  RealtimeVideoInterpolationOptions.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Options for two-times interpolation of the remote video preview.</summary>
/// <param name="ModelDirectory">RIFE v4.6 model directory. Null uses models/rife-v4.6 beside the native DLL.</param>
/// <param name="DeviceIndex">Vulkan GPU index, or -1 for explicit CPU processing.</param>
/// <param name="CpuThreads">CPU thread count in the range 1 through 32.</param>
/// <param name="BufferMilliseconds">Preview buffering in milliseconds, from 50 through 500.</param>
/// <remarks>Default is disabled. Video buffering adds delay relative to native audio playback.
/// Remote media callbacks retain the original video/audio cadence and timestamps.</remarks>
public sealed record RealtimeVideoInterpolationOptions(
    string? ModelDirectory = null,
    int DeviceIndex = 0,
    uint CpuThreads = 4,
    uint BufferMilliseconds = 125);

/// <summary>The current interpolation state, counters, and any recoverable processing failure.</summary>
/// <param name="Enabled">Whether the preview processor is enabled.</param>
/// <param name="InputFrames">Original frames admitted while enabled.</param>
/// <param name="InterpolatedFrames">Intermediate frames delivered to the preview.</param>
/// <param name="SkippedFrames">Intermediate frames skipped due to cuts, deadlines, or queue pressure.</param>
/// <param name="Error">The processing failure that restored original preview delivery, or null.</param>
public sealed record RealtimeVideoInterpolationStatus(
    bool Enabled, ulong InputFrames, ulong InterpolatedFrames, ulong SkippedFrames, XmaxException? Error);
