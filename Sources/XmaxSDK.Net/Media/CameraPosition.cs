//
//  CameraPosition.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The enclosure position reported by a camera.
/// </summary>
public enum CameraPosition
{
    /// <summary>
    /// The camera has no known front or back panel.
    /// </summary>
    Unspecified = 0,

    /// <summary>
    /// The camera faces the user.
    /// </summary>
    Front = 1,

    /// <summary>
    /// The camera faces away from the user.
    /// </summary>
    Back = 2
}
