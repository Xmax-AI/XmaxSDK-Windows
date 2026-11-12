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

/// <summary>Camera encoding resolution presets. Supported presets depend on the model.</summary>
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
    Resolution1536x1152,
    /// <summary>Portrait, 704 by 1280 pixels.</summary>
    Resolution704x1280,
    /// <summary>Landscape, 1280 by 704 pixels.</summary>
    Resolution1280x704,
    /// <summary>Square, 960 by 960 pixels.</summary>
    Resolution960x960,
    /// <summary>Landscape 4:3, 1024 by 768 pixels.</summary>
    Resolution1024x768,
    /// <summary>Portrait 3:4, 768 by 1024 pixels.</summary>
    Resolution768x1024
}
