//
//  RealtimeVideoBeautyOptions.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>Beauty applied once to generated results before preview and public media output.</summary>
/// <remarks>Enabled with all strengths at zero by default. Camera input and uplink are unchanged.</remarks>
public sealed record RealtimeVideoBeautyOptions(
    float Smoothness = 0,
    float Lightening = 0,
    float Sharpness = 0,
    float Redness = 0)
{
    /// <summary>Rejects nonfinite strengths and values outside 0 through 1.</summary>
    public void Validate()
    {
        foreach (float value in new[] { Smoothness, Lightening, Sharpness, Redness })
            if (!float.IsFinite(value) || value < 0 || value > 1)
                throw new XmaxException(-1, "Beauty strengths must be finite numbers from 0 to 1");
    }
    internal unsafe xmax_realtime_video_beauty_options Native => new()
    {
        struct_size = (uint)sizeof(xmax_realtime_video_beauty_options),
        smoothness = Smoothness, lightening = Lightening, sharpness = Sharpness, redness = Redness
    };
}
