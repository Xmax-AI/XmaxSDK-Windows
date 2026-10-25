//
//  EncodingConfiguration.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// RTC bitrate limits and the preference used when adapting the encoder.
/// </summary>
/// <param name="MinimumBitrateKbps">The minimum bitrate, in kbps; -1 selects the SDK policy and 0 omits the minimum limit.</param>
/// <param name="MaximumBitrateKbps">The maximum bitrate, in kbps; 0 selects the SDK estimate, no lower than the minimum.</param>
/// <param name="Preference">The tradeoff between frame rate and image quality during constrained conditions.</param>
public sealed record EncodingConfiguration(
    int MinimumBitrateKbps = -1,
    int MaximumBitrateKbps = 0,
    EncodingPreference Preference = EncodingPreference.Automatic
);
