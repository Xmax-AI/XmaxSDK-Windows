//
//  ExternalAudioSampleFormat.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Little-endian PCM sample types and channel layouts for external audio.</summary>
public enum ExternalAudioSampleFormat
{
    /// <summary>Interleaved signed 16-bit PCM.</summary>
    Pcm16 = 0,
    /// <summary>Interleaved 32-bit IEEE floating-point PCM.</summary>
    Float32 = 1,
    /// <summary>Interleaved signed packed 24-bit PCM.</summary>
    Pcm24 = 2,
    /// <summary>Interleaved signed 32-bit PCM.</summary>
    Pcm32 = 3,
    /// <summary>Interleaved 64-bit IEEE floating-point PCM.</summary>
    Float64 = 4,
    /// <summary>Signed 16-bit PCM in contiguous channel planes.</summary>
    PlanarPcm16 = 5,
    /// <summary>32-bit IEEE floating-point PCM in contiguous channel planes.</summary>
    PlanarFloat32 = 6,
    /// <summary>Signed packed 24-bit PCM in contiguous channel planes.</summary>
    PlanarPcm24 = 7,
    /// <summary>Signed 32-bit PCM in contiguous channel planes.</summary>
    PlanarPcm32 = 8,
    /// <summary>64-bit IEEE floating-point PCM in contiguous channel planes.</summary>
    PlanarFloat64 = 9
}
