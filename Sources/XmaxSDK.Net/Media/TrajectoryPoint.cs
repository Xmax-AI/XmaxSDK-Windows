//
//  TrajectoryPoint.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A trajectory coordinate whose units are determined by the interaction API.
/// </summary>
/// <param name="X">The horizontal coordinate.</param>
/// <param name="Y">The vertical coordinate.</param>
/// <remarks>
/// Input coordinates use viewport units. Coordinates returned by GetTrajectory are normalized to the video.
/// </remarks>
public readonly record struct TrajectoryPoint(
    double X,
    double Y
);
