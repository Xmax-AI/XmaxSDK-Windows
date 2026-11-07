//
//  TrajectoryGesture.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;

namespace Xmax.SDK.Rendering;

internal sealed class TrajectoryGesture
{
    private readonly List<Stroke> strokes = [];
    private TrajectoryPoint? active;
    private Guid activeId;
    private TrajectoryPoint? previous;
    private long lastSample;
    private long lastStroke;
    private long interactionRevision;

    internal readonly record struct Stroke(TrajectoryPoint From, TrajectoryPoint To, long Time);

    internal IReadOnlyList<Stroke> Strokes => strokes;
    internal bool Active => active.HasValue;
    internal ITrajectoryEffectRenderer? Renderer { get; set; }

    internal bool Begin(TrajectoryPoint point, VideoViewport viewport, long revision)
    {
        if (!viewport.Contains(point)) return false;
        active = point;
        activeId = Guid.NewGuid();
        previous = null;
        lastSample = lastStroke = 0;
        interactionRevision = revision;
        return true;
    }

    internal void Move(TrajectoryPoint point) { if (Active) active = point; }
    internal void End()
    {
        bool started = previous.HasValue;
        active = previous = null;
        if (started) Renderer?.RenderEnded(activeId);
    }

    internal void Reset()
    {
        End();
        strokes.Clear();
        Renderer?.Reset();
    }

    internal bool Update(RealtimeVideoTrack? track, VideoViewport viewport, bool force = false)
    {
        long now = Stopwatch.GetTimestamp();
        int expired = 0;
        while (expired < strokes.Count && Opacity(strokes[expired], now) <= 0) ++expired;
        if (expired > 0) strokes.RemoveRange(0, expired);
        if (track?.IsRemote != true || active is not { } point || !viewport.IsValid)
            return expired > 0 || strokes.Count > 0;
        if (track.InteractionRevision != interactionRevision)
        {
            Reset();
            return true;
        }

        point = viewport.Clamp(point);
        if (force || Stopwatch.GetElapsedTime(lastSample, now).TotalSeconds >= 1d / 30)
        {
            Span<TrajectoryPoint> sample = stackalloc TrajectoryPoint[] { point };
            if (!track.TrySubmitInteraction(sample, viewport.Width, viewport.Height, viewport.VideoWidth,
                viewport.VideoHeight, viewport.Mirrored, viewport.Mode))
            {
                Reset();
                return true;
            }
            lastSample = now;
        }
        if (Renderer is { } renderer)
        {
            var sample = new TrajectoryEffectSample(
                activeId,
                point,
                viewport.Normalize(point),
                now / (double)Stopwatch.Frequency
            );
            var last = previous;
            previous = point;
            if (!last.HasValue)
                renderer.RenderBegan(sample);
            else if (last.Value != point)
                renderer.RenderMoved(sample);
            return true;
        }

        if (point != previous || Stopwatch.GetElapsedTime(lastStroke, now).TotalMilliseconds >= 100)
        {
            strokes.Add(new(previous ?? point, point, now));
            previous = point;
            lastStroke = now;
        }
        return true;
    }

    internal static double Opacity(Stroke stroke, long now) =>
        Math.Clamp(1 - Stopwatch.GetElapsedTime(stroke.Time, now).TotalMilliseconds / 420, 0, 1);
}

internal readonly record struct VideoViewport(
    double Width, double Height, uint VideoWidth, uint VideoHeight, bool Mirrored, VideoContentMode Mode)
{
    internal bool IsValid => Width > 0 && Height > 0 && VideoWidth > 0 && VideoHeight > 0;

    private (double X, double Y) Inset
    {
        get
        {
            if (Mode == VideoContentMode.Fill) return (0, 0);
            double scale = Math.Min(Width / VideoWidth, Height / VideoHeight);
            return ((Width - VideoWidth * scale) / 2, (Height - VideoHeight * scale) / 2);
        }
    }

    internal bool Contains(TrajectoryPoint point)
    {
        if (!IsValid) return false;
        var (x, y) = Inset;
        return point.X >= x && point.X <= Width - x && point.Y >= y && point.Y <= Height - y;
    }

    internal TrajectoryPoint Normalize(
        TrajectoryPoint point
    )
    {
        double scaleX = Width / VideoWidth;
        double scaleY = Height / VideoHeight;
        double scale = Mode == VideoContentMode.Fill ? Math.Max(scaleX, scaleY) : Math.Min(scaleX, scaleY);
        double mediaWidth = VideoWidth * scale;
        double mediaHeight = VideoHeight * scale;
        return new(
            (point.X - (Width - mediaWidth) / 2) / mediaWidth,
            (point.Y - (Height - mediaHeight) / 2) / mediaHeight
        );
    }

    internal TrajectoryPoint Clamp(TrajectoryPoint point)
    {
        var (x, y) = Inset;
        return new(Math.Clamp(point.X, x, Width - x), Math.Clamp(point.Y, y, Height - y));
    }
}
