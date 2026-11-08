//
//  TrajectoryEffectVisual.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;
using Effect = Xmax.SDK.Rendering.DefaultTrajectoryEffect;

namespace Xmax.SDK.WinUI;

internal sealed class TrajectoryEffectVisual : Grid
{
    private readonly Canvas trails = new();
    private readonly Canvas pointer = new() { Visibility = Visibility.Collapsed };
    private readonly List<Trail> segments = [];
    private readonly Ellipse[] rings = new Ellipse[2];
    private readonly Ellipse[] particles = new Ellipse[4];
    private readonly Ellipse headGlow = new() { Fill = Radial(Effect.HeadGlow) };
    private readonly Ellipse head = new() { Fill = new SolidColorBrush(Microsoft.UI.Colors.White) };
    private readonly Brush outerGlow = Linear(Effect.OuterGlow);
    private readonly Brush innerGlow = Linear(Effect.InnerGlow);
    private readonly Brush core = new SolidColorBrush(Color.FromArgb((byte)Math.Round(255 * Effect.CoreOpacity), 255, 255, 255));

    internal TrajectoryEffectVisual()
    {
        Children.Add(trails);
        Children.Add(pointer);
        var ringBrush = new SolidColorBrush(Green(1));
        var particleBrush = Radial(Effect.ParticleGlow);
        for (int i = 0; i < rings.Length; ++i)
        {
            rings[i] = new Ellipse { Stroke = ringBrush, StrokeThickness = Effect.RingWidth };
            pointer.Children.Add(rings[i]);
        }
        for (int i = 0; i < particles.Length; ++i)
        {
            particles[i] = new Ellipse { Fill = particleBrush };
            pointer.Children.Add(particles[i]);
        }
        pointer.Children.Add(headGlow);
        pointer.Children.Add(head);
    }

    internal void Clear()
    {
        segments.Clear();
        trails.Children.Clear();
        pointer.Visibility = Visibility.Collapsed;
    }

    internal void Draw(Effect effect, long now)
    {
        int count = effect.Strokes.Count;
        while (segments.Count > count)
        {
            segments.RemoveAt(segments.Count - 1);
            trails.Children.RemoveAt(trails.Children.Count - 1);
        }
        for (int i = 0; i < count; ++i)
        {
            if (i == segments.Count)
            {
                var segment = new Trail(outerGlow, innerGlow, core);
                segments.Add(segment);
                trails.Children.Add(segment.View);
            }
            segments[i].Draw(effect.Strokes[i], now);
        }

        if (effect.Head is not { } current)
        {
            pointer.Visibility = Visibility.Collapsed;
            return;
        }

        pointer.Visibility = Visibility.Visible;
        for (int i = 0; i < rings.Length; ++i)
        {
            var ring = current.Ring(i, now);
            Place(rings[i], ring with { Radius = ring.Radius + Effect.RingWidth / 2 });
        }
        for (int i = 0; i < particles.Length; ++i)
            Place(particles[i], current.Particle(i, now));
        Place(headGlow, new(current.Center, Effect.HeadGlowRadius, 1));
        Place(head, new(current.Center, Effect.HeadRadius, 1));
    }

    private static void Place(Ellipse shape, Effect.Sprite sprite)
    {
        shape.Width = shape.Height = 2 * sprite.Radius;
        Canvas.SetLeft(shape, sprite.Center.X - sprite.Radius);
        Canvas.SetTop(shape, sprite.Center.Y - sprite.Radius);
        shape.Opacity = sprite.Opacity;
    }

    private static Color Green(double opacity) =>
        Color.FromArgb((byte)Math.Round(255 * opacity), 0, 255, Effect.GreenBlue);

    private static Brush Linear(ReadOnlySpan<Effect.GradientStop> stops)
    {
        var brush = new LinearGradientBrush { StartPoint = new(0, 0), EndPoint = new(0, 1) };
        foreach (var stop in stops) brush.GradientStops.Add(ToGradientStop(stop));
        return brush;
    }

    private static Brush Radial(ReadOnlySpan<Effect.GradientStop> stops)
    {
        var brush = new RadialGradientBrush();
        foreach (var stop in stops) brush.GradientStops.Add(ToGradientStop(stop));
        return brush;
    }

    private static GradientStop ToGradientStop(Effect.GradientStop stop) =>
        new() { Color = Green(stop.Opacity), Offset = stop.Offset };

    private sealed class Trail
    {
        internal Canvas View { get; } = new() { Height = 2 * Effect.OuterRadius };
        private readonly RotateTransform rotation = new() { CenterX = Effect.OuterRadius, CenterY = Effect.OuterRadius };
        private readonly Rectangle outer, inner, core;

        internal Trail(Brush outerGlow, Brush innerGlow, Brush coreColor)
        {
            View.RenderTransform = rotation;
            outer = AddBand(Effect.OuterRadius, outerGlow);
            inner = AddBand(Effect.InnerRadius, innerGlow);
            core = AddBand(Effect.CoreWidth / 2, coreColor);
        }

        private Rectangle AddBand(double radius, Brush fill)
        {
            var rectangle = new Rectangle { Height = 2 * radius, RadiusX = radius, RadiusY = radius, Fill = fill };
            Canvas.SetLeft(rectangle, Effect.OuterRadius - radius);
            Canvas.SetTop(rectangle, Effect.OuterRadius - radius);
            View.Children.Add(rectangle);
            return rectangle;
        }

        internal void Draw(Effect.Stroke stroke, long now)
        {
            var length = stroke.Length;
            View.Width = outer.Width = length + outer.Height;
            inner.Width = length + inner.Height;
            core.Width = length + core.Height;
            Canvas.SetLeft(View, stroke.From.X - Effect.OuterRadius);
            Canvas.SetTop(View, stroke.From.Y - Effect.OuterRadius);
            rotation.Angle = stroke.Angle;
            View.Opacity = stroke.Opacity(now);
        }
    }
}
