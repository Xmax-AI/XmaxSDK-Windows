//
//  TrajectoryEffectDrawing.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Media;
using Effect = Xmax.SDK.Rendering.DefaultTrajectoryEffect;

namespace Xmax.SDK.Wpf;

internal static class TrajectoryEffectDrawing
{
    private static readonly Brush OuterGlow = Linear(Effect.OuterGlow);
    private static readonly Brush InnerGlow = Linear(Effect.InnerGlow);
    private static readonly Brush HeadGlow = Radial(Effect.HeadGlow);
    private static readonly Brush ParticleGlow = Radial(Effect.ParticleGlow);
    private static readonly Brush Core = Frozen(new SolidColorBrush(
        Color.FromArgb((byte)Math.Round(255 * Effect.CoreOpacity), 255, 255, 255)));
    private static readonly Pen Ring = Frozen(new Pen(new SolidColorBrush(Green(1)), Effect.RingWidth));

    internal static void Draw(DrawingContext drawing, Effect effect, long now)
    {
        foreach (var stroke in effect.Strokes)
        {
            var transform = Matrix.Identity;
            transform.Rotate(stroke.Angle);
            transform.Translate(stroke.From.X, stroke.From.Y);
            drawing.PushTransform(new MatrixTransform(transform));
            drawing.PushOpacity(stroke.Opacity(now));
            double length = stroke.Length;
            DrawBand(drawing, OuterGlow, length, Effect.OuterRadius);
            DrawBand(drawing, InnerGlow, length, Effect.InnerRadius);
            DrawBand(drawing, Core, length, Effect.CoreWidth / 2);
            drawing.Pop();
            drawing.Pop();
        }

        if (effect.Head is not { } head) return;
        for (int i = 0; i < 2; ++i)
            DrawSprite(drawing, head.Ring(i, now), null, Ring);
        for (int i = 0; i < 4; ++i)
            DrawSprite(drawing, head.Particle(i, now), ParticleGlow, null);
        DrawSprite(drawing, new(head.Center, Effect.HeadGlowRadius, 1), HeadGlow, null);
        DrawSprite(drawing, new(head.Center, Effect.HeadRadius, 1), Brushes.White, null);
    }

    private static void DrawBand(DrawingContext drawing, Brush brush, double length, double radius) =>
        drawing.DrawRoundedRectangle(brush, null, new(-radius, -radius, length + 2 * radius, 2 * radius), radius, radius);

    private static void DrawSprite(DrawingContext drawing, Effect.Sprite sprite, Brush? fill, Pen? pen)
    {
        drawing.PushOpacity(sprite.Opacity);
        drawing.DrawEllipse(fill, pen, new(sprite.Center.X, sprite.Center.Y), sprite.Radius, sprite.Radius);
        drawing.Pop();
    }

    private static Color Green(double opacity) =>
        Color.FromArgb((byte)Math.Round(255 * opacity), 0, 255, Effect.GreenBlue);

    private static Brush Linear(ReadOnlySpan<Effect.GradientStop> stops) =>
        Frozen(new LinearGradientBrush(Stops(stops), new(0, 0), new(0, 1)));

    private static Brush Radial(ReadOnlySpan<Effect.GradientStop> stops) =>
        Frozen(new RadialGradientBrush(Stops(stops)));

    private static GradientStopCollection Stops(ReadOnlySpan<Effect.GradientStop> stops)
    {
        var result = new GradientStopCollection(stops.Length);
        foreach (var stop in stops)
            result.Add(new GradientStop(Green(stop.Opacity), stop.Offset));
        return result;
    }

    private static T Frozen<T>(T value) where T : Freezable
    {
        value.Freeze();
        return value;
    }
}
