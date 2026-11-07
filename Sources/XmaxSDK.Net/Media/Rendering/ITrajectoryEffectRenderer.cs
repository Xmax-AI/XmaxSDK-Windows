//
//  ITrajectoryEffectRenderer.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK.Rendering;

/// <summary>
/// One pointer sample delivered to a trajectory effect renderer.
/// </summary>
/// <param name="Id">The identifier shared by all samples from this gesture.</param>
/// <param name="Location">The pointer location in view units.</param>
/// <param name="NormalizedLocation">The location normalized to the full displayed video rectangle.</param>
/// <param name="TimestampSeconds">The monotonic sample time in seconds.</param>
public readonly record struct TrajectoryEffectSample(
    Guid Id,
    TrajectoryPoint Location,
    TrajectoryPoint NormalizedLocation,
    double TimestampSeconds
);

/// <summary>
/// Receives pointer samples to draw a custom effect on the video view's UI thread.
/// </summary>
/// <remarks>
/// XmaxSDK owns sampling, coordinate mapping, and submission to the current generation task.
/// </remarks>
public interface ITrajectoryEffectRenderer
{
    /// <summary>
    /// Starts drawing a new gesture.
    /// </summary>
    /// <param name="sample">The initial pointer sample.</param>
    void RenderBegan(
        TrajectoryEffectSample sample
    );

    /// <summary>
    /// Moves an active gesture.
    /// </summary>
    /// <param name="sample">The latest pointer sample.</param>
    void RenderMoved(
        TrajectoryEffectSample sample
    );

    /// <summary>
    /// Ends the active gesture and allows the effect to finish.
    /// </summary>
    /// <param name="id">The identifier of the completed gesture.</param>
    void RenderEnded(
        Guid id
    );

    /// <summary>
    /// Clears all effects and cancels any related animations.
    /// </summary>
    void Reset();
}
