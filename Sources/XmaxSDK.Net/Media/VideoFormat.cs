using Xmax.SDK.Interop;

namespace Xmax.SDK;

public readonly record struct VideoFormat(uint Width, uint Height, uint FramesPerSecond = 30)
{
    internal xmax_video_format Native => new() { width = Width, height = Height, fps = FramesPerSecond };
    internal static VideoFormat From(xmax_video_format value) => new(value.width, value.height, value.fps);
    public override string ToString() => $"{Width} × {Height} @ {FramesPerSecond} fps";
}
