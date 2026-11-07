//
//  RealtimeCameraPreset.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The five camera encoding resolution presets supported by X2.1.</summary>
public enum RealtimeCameraPreset
{
    /// <summary>Landscape 16:9, 1920 by 1080 pixels.</summary>
    Resolution1920x1080,
    /// <summary>Portrait 9:16, 1080 by 1920 pixels.</summary>
    Resolution1080x1920,
    /// <summary>Square, 1408 by 1408 pixels.</summary>
    Resolution1408x1408,
    /// <summary>Portrait 3:4, 1152 by 1536 pixels.</summary>
    Resolution1152x1536,
    /// <summary>Landscape 4:3, 1536 by 1152 pixels.</summary>
    Resolution1536x1152
}
