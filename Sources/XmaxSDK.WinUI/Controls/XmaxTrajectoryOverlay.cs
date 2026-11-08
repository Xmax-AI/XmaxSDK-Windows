//
//  XmaxTrajectoryOverlay.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.WinUI;

/// <summary>Samples result-view gestures and draws the default trajectory effect over a custom WinUI video view.</summary>
public sealed class XmaxTrajectoryOverlay : Canvas
{
    private readonly Grid host;
    private readonly TrajectoryGesture gesture = new();
    private readonly RectangleGeometry clip = new();
    private Border? customEffect;
    private readonly TrajectoryEffectVisual effect = new();
    private RealtimeVideoTrack? track;
    private uint width, height;
    private bool mirrored;
    private VideoContentMode mode;
    private Pointer? pointer;
    private bool enabled = true;
    private bool rendering;

    /// <summary>
    /// Attaches a trajectory overlay to the supplied video host.
    /// </summary>
    /// <param name="host">The grid containing the video, accessed on its UI thread.</param>
    public XmaxTrajectoryOverlay(
        Grid host
    )
    {
        this.host = host;
        IsHitTestVisible = false;
        Clip = clip;
        Children.Add(effect);
        host.Children.Add(this);
        host.SizeChanged += (_, args) =>
        {
            clip.Rect = new(0, 0, args.NewSize.Width, args.NewSize.Height);
            effect.Width = args.NewSize.Width;
            effect.Height = args.NewSize.Height;
            if (customEffect?.Clip is RectangleGeometry geometry)
                geometry.Rect = clip.Rect;
        };
        host.PointerPressed += Begin;
        host.PointerMoved += Move;
        host.PointerReleased += Finish;
        host.PointerCanceled += CancelPointer;
        host.PointerCaptureLost += CancelPointer;
        host.Unloaded += (_, _) => Reset();
    }

    /// <summary>Gets or sets the result video track supplying interaction.</summary>
    public RealtimeVideoTrack? Track
    {
        get => track;
        set { if (track != value) { Reset(); track = value; } }
    }

    /// <summary>Gets or sets whether primary pointer gestures are enabled.</summary>
    public bool InteractionEnabled
    {
        get => enabled;
        set { enabled = value; if (!value) Reset(); }
    }

    /// <summary>
    /// Gets or sets the renderer for the displayed effect. Null restores the SDK effect.
    /// </summary>
    public IXmaxTrajectoryEffectRenderer? TrajectoryRenderer
    {
        get => gesture.Renderer as IXmaxTrajectoryEffectRenderer;
        set
        {
            if (ReferenceEquals(value, gesture.Renderer)) return;
            Reset();
            if (customEffect is { } previous)
            {
                previous.Child = null;
                host.Children.Remove(previous);
                customEffect = null;
            }
            gesture.Renderer = value;
            if (value is null) return;

            customEffect = new Border
            {
                Child = value.View,
                IsHitTestVisible = false,
                Clip = new RectangleGeometry { Rect = clip.Rect }
            };
            host.Children.Add(customEffect);
        }
    }

    /// <summary>
    /// Updates interaction geometry to match the displayed frame.
    /// </summary>
    /// <param name="videoWidth">The frame width, in pixels, or zero to disable interaction.</param>
    /// <param name="videoHeight">The frame height, in pixels, or zero to disable interaction.</param>
    /// <param name="frameMirrored">Whether the displayed frame is horizontally mirrored.</param>
    /// <param name="contentMode">The aspect ratio layout used to display the frame.</param>
    public void UpdateFrame(
        uint videoWidth,
        uint videoHeight,
        bool frameMirrored,
        VideoContentMode contentMode
    )
    {
        width = videoWidth;
        height = videoHeight;
        mirrored = frameMirrored;
        mode = contentMode;
    }

    private VideoViewport Viewport
    {
        get
        {
            var source = width != 0 && height != 0 ? track?.InteractionFormat : null;
            return new(host.ActualWidth, host.ActualHeight, source?.Width ?? width, source?.Height ?? height, mirrored, mode);
        }
    }

    private void Begin(object sender, PointerRoutedEventArgs args)
    {
        var point = args.GetCurrentPoint(host);
        if (!enabled || track?.IsRemote != true || pointer is not null ||
            (point.PointerDeviceType == PointerDeviceType.Mouse && !point.Properties.IsLeftButtonPressed) ||
            !gesture.Begin(new(point.Position.X, point.Position.Y), Viewport, track.InteractionRevision))
            return;
        if (!host.CapturePointer(args.Pointer)) { gesture.End(); return; }
        pointer = args.Pointer;
        gesture.Update(track, Viewport, force: true);
        if (!gesture.Active) { Reset(); return; }
        if (!rendering)
        {
            rendering = true;
            CompositionTarget.Rendering += Render;
        }
        args.Handled = true;
    }

    private void Move(object sender, PointerRoutedEventArgs args)
    {
        if (pointer?.PointerId != args.Pointer.PointerId) return;
        var point = args.GetCurrentPoint(host).Position;
        gesture.Move(new(point.X, point.Y));
        args.Handled = true;
    }

    private void Finish(object sender, PointerRoutedEventArgs args)
    {
        if (pointer?.PointerId != args.Pointer.PointerId) return;
        Move(sender, args);
        gesture.Update(track, Viewport, force: true);
        gesture.End();
        effect.Draw(gesture.Effect, Stopwatch.GetTimestamp());
        pointer = null;
        host.ReleasePointerCapture(args.Pointer);
    }

    private void CancelPointer(object sender, PointerRoutedEventArgs args)
    {
        if (pointer?.PointerId == args.Pointer.PointerId) Reset();
    }

    private void Reset()
    {
        var captured = pointer;
        pointer = null;
        if (captured is not null) host.ReleasePointerCapture(captured);
        gesture.Reset();
        effect.Clear();
        StopRendering();
    }

    private void StopRendering()
    {
        if (!rendering) return;
        CompositionTarget.Rendering -= Render;
        rendering = false;
    }

    private void Render(object? sender, object args)
    {
        gesture.Update(track, Viewport);
        if (!gesture.Active && pointer is not null)
        {
            Reset();
            return;
        }
        effect.Draw(gesture.Effect, Stopwatch.GetTimestamp());
        if (!gesture.Active && gesture.Effect.Strokes.Count == 0)
            StopRendering();
    }
}
