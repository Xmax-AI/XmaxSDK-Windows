//
//  CameraRotation.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//
namespace Xmax.SDK;

/// <summary>Clockwise camera rotation applied before output mirroring.</summary>
public enum CameraRotation
{
    /// <summary>No additional rotation.</summary>
    None = 0,
    /// <summary>Rotate clockwise by 90 degrees.</summary>
    Clockwise90 = 90,
    /// <summary>Rotate by 180 degrees.</summary>
    Clockwise180 = 180,
    /// <summary>Rotate clockwise by 270 degrees.</summary>
    Clockwise270 = 270
}
