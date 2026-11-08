//
//  DefaultTrajectoryEffect.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;

namespace Xmax.SDK.Rendering;

internal sealed class DefaultTrajectoryEffect
{
    internal const byte GreenBlue = 100;
    internal const double CoreWidth = 3;
    internal const double CoreOpacity = .82;
    internal const double OuterRadius = 15;
    internal const double InnerRadius = 8;
    internal const double RingWidth = 2;
    internal const double HeadRadius = 5;
    internal const double HeadGlowRadius = 16;
    internal const double TrailLifetime = 64d / 60;

    internal readonly record struct GradientStop(double Offset, double Opacity);

    private static readonly GradientStop[] OuterGlowStops =
        [new(0, 0), new(.2, .06), new(.5, .22), new(.8, .06), new(1, 0)];
    private static readonly GradientStop[] InnerGlowStops =
        [new(0, 0), new(.2, .16), new(.5, .52), new(.8, .16), new(1, 0)];
    private static readonly GradientStop[] HeadGlowStops = [new(0, .9), new(.4, .54), new(1, 0)];
    private static readonly GradientStop[] ParticleGlowStops = [new(0, 1), new(1, 0)];

    internal static ReadOnlySpan<GradientStop> OuterGlow => OuterGlowStops;
    internal static ReadOnlySpan<GradientStop> InnerGlow => InnerGlowStops;
    internal static ReadOnlySpan<GradientStop> HeadGlow => HeadGlowStops;
    internal static ReadOnlySpan<GradientStop> ParticleGlow => ParticleGlowStops;

    internal readonly record struct Stroke(TrajectoryPoint From, TrajectoryPoint To, long Time)
    {
        internal double Length
        {
            get
            {
                double dx = To.X - From.X, dy = To.Y - From.Y;
                return Math.Sqrt(dx * dx + dy * dy);
            }
        }

        internal double Angle => Math.Atan2(To.Y - From.Y, To.X - From.X) * 180 / Math.PI;

        internal double Opacity(long now) =>
            Math.Pow(.95, Stopwatch.GetElapsedTime(Time, now).TotalSeconds * 60);
    }

    internal readonly record struct Sprite(TrajectoryPoint Center, double Radius, double Opacity);
    internal readonly record struct Pointer(TrajectoryPoint Center, long Started)
    {
        internal Sprite Ring(int index, long now)
        {
            double elapsed = Stopwatch.GetElapsedTime(Started, now).TotalSeconds;
            double pulse = (Math.Sin((elapsed * 1.2 + index * .5) * Math.Tau) + 1) / 2;
            return new(Center, 14 + index * 18 + pulse * 8, .5 * (1 - index * .2) * (.5 + pulse * .5));
        }

        internal Sprite Particle(int index, long now)
        {
            double elapsed = Stopwatch.GetElapsedTime(Started, now).TotalSeconds;
            double angle = index / 4d * Math.Tau + elapsed * .06 * (index % 2 == 0 ? 1 : -1);
            return new(new(Center.X + Math.Cos(angle) * 22, Center.Y + Math.Sin(angle) * 22),
                6, .6 * (.6 + Math.Sin(elapsed * 3 + index) * .4));
        }
    }

    private readonly List<Stroke> strokes = [];
    internal IReadOnlyList<Stroke> Strokes => strokes;
    internal Pointer? Head { get; private set; }

    internal void Sample(TrajectoryPoint point, long now)
    {
        if (Head is not { } head)
        {
            Head = new(point, now);
            return;
        }

        var stroke = new Stroke(head.Center, point, now);
        double dx = point.X - head.Center.X, dy = point.Y - head.Center.Y;
        if (dx * dx + dy * dy >= .25)
            strokes.Add(stroke);
        Head = head with { Center = point };
    }

    internal bool Advance(long now)
    {
        int expired = 0;
        while (expired < strokes.Count && Stopwatch.GetElapsedTime(strokes[expired].Time, now).TotalSeconds >= TrailLifetime)
            ++expired;
        if (expired > 0) strokes.RemoveRange(0, expired);
        return expired > 0 || strokes.Count > 0;
    }

    internal void End() => Head = null;

    internal void Reset()
    {
        End();
        strokes.Clear();
    }
}
