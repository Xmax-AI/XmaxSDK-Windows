//
//  XmaxRealtimeVideoView.Interaction.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.Wpf;

public sealed partial class XmaxRealtimeVideoView
{
    private readonly TrajectoryGesture trajectory = new();
    private static readonly Pen TrajectoryPen = CreateTrajectoryPen();
    private Border? customEffect;

    /// <summary>Identifies the result view's pointer interaction setting.</summary>
    public static readonly DependencyProperty IsInteractionEnabledProperty = DependencyProperty.Register(
        nameof(IsInteractionEnabled), typeof(bool), typeof(XmaxRealtimeVideoView),
        new PropertyMetadata(true, (owner, _) => ((XmaxRealtimeVideoView)owner).ResetTrajectory()));

    /// <summary>Gets or sets whether primary pointer gestures are enabled on result video.</summary>
    public bool IsInteractionEnabled
    {
        get => (bool)GetValue(IsInteractionEnabledProperty);
        set => SetValue(IsInteractionEnabledProperty, value);
    }

    /// <summary>
    /// Gets or sets the renderer for the displayed trajectory effect. Null restores the SDK effect.
    /// </summary>
    public IXmaxTrajectoryEffectRenderer? TrajectoryRenderer
    {
        get => trajectory.Renderer as IXmaxTrajectoryEffectRenderer;
        set
        {
            if (ReferenceEquals(value, trajectory.Renderer)) return;
            ResetTrajectory();
            if (customEffect is { } previous)
            {
                previous.Child = null;
                RemoveVisualChild(previous);
                customEffect = null;
            }
            trajectory.Renderer = value;
            if (value is not null)
            {
                customEffect = new Border { Child = value.View, IsHitTestVisible = false };
                AddVisualChild(customEffect);
            }
            InvalidateMeasure();
        }
    }

    /// <inheritdoc />
    protected override int VisualChildrenCount => customEffect is null ? 0 : 1;

    /// <inheritdoc />
    protected override Visual GetVisualChild(
        int index
    ) => index == 0 && customEffect is not null
        ? customEffect
        : throw new ArgumentOutOfRangeException(nameof(index));

    /// <inheritdoc />
    protected override Size MeasureOverride(
        Size availableSize
    )
    {
        customEffect?.Measure(availableSize);
        return base.MeasureOverride(availableSize);
    }

    /// <inheritdoc />
    protected override Size ArrangeOverride(
        Size finalSize
    )
    {
        customEffect?.Arrange(new Rect(finalSize));
        return finalSize;
    }

    private VideoViewport Viewport
    {
        get
        {
            var source = VideoWidth != 0 && VideoHeight != 0 ? ResultTrack?.InteractionFormat : null;
            return new(ActualWidth, ActualHeight, source?.Width ?? VideoWidth, source?.Height ?? VideoHeight, DisplayMirrored, ContentMode);
        }
    }
    private RealtimeVideoTrack? ResultTrack => VideoSource as RealtimeVideoTrack;

    /// <inheritdoc />
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs args)
    {
        base.OnMouseLeftButtonDown(args);
        var point = args.GetPosition(this);
        if (!IsInteractionEnabled || ResultTrack is not { IsRemote: true } track ||
            !trajectory.Begin(new(point.X, point.Y), Viewport, track.InteractionRevision)) return;
        if (!CaptureMouse()) { trajectory.End(); return; }
        trajectory.Update(ResultTrack, Viewport, force: true);
        InvalidateVisual();
        args.Handled = true;
    }

    /// <inheritdoc />
    protected override void OnMouseMove(MouseEventArgs args)
    {
        base.OnMouseMove(args);
        if (!IsMouseCaptured) return;
        var point = args.GetPosition(this);
        trajectory.Move(new(point.X, point.Y));
        args.Handled = true;
    }

    /// <inheritdoc />
    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs args)
    {
        base.OnMouseLeftButtonUp(args);
        if (!IsMouseCaptured) return;
        var point = args.GetPosition(this);
        trajectory.Move(new(point.X, point.Y));
        trajectory.Update(ResultTrack, Viewport, force: true);
        trajectory.End();
        ReleaseMouseCapture();
        args.Handled = true;
    }

    /// <inheritdoc />
    protected override void OnLostMouseCapture(MouseEventArgs args)
    {
        base.OnLostMouseCapture(args);
        if (trajectory.Active) ResetTrajectory();
    }

    private void ResetTrajectory()
    {
        trajectory.Reset();
        if (IsMouseCaptured) ReleaseMouseCapture();
        InvalidateVisual();
    }

    private void DrawTrajectory(DrawingContext drawing)
    {
        long now = Stopwatch.GetTimestamp();
        foreach (var stroke in trajectory.Strokes)
        {
            drawing.PushOpacity(TrajectoryGesture.Opacity(stroke, now));
            if (stroke.From == stroke.To)
                drawing.DrawEllipse(TrajectoryPen.Brush, null, new(stroke.To.X, stroke.To.Y), 3, 3);
            else
                drawing.DrawLine(TrajectoryPen, new(stroke.From.X, stroke.From.Y), new(stroke.To.X, stroke.To.Y));
            drawing.Pop();
        }
    }

    private static Pen CreateTrajectoryPen()
    {
        var pen = new Pen(new SolidColorBrush(Color.FromRgb(58, 255, 137)), 5)
        {
            StartLineCap = PenLineCap.Round,
            EndLineCap = PenLineCap.Round
        };
        pen.Freeze();
        return pen;
    }
}
