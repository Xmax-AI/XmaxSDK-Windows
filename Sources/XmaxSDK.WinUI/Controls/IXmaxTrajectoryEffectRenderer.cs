//
//  IXmaxTrajectoryEffectRenderer.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.WinUI;

/// <summary>
/// Provides a WinUI view that draws custom trajectory effects over the video.
/// </summary>
public interface IXmaxTrajectoryEffectRenderer : ITrajectoryEffectRenderer
{
    /// <summary>
    /// Gets the unparented view to mount over the video.
    /// </summary>
    UIElement View { get; }
}
