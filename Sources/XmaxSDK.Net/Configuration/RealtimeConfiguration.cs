namespace Xmax.SDK;

public sealed record RealtimeConfiguration
{
    public string Model { get; init; } = "x2.1";
    public bool AdaptiveQualityEnabled { get; init; } = true;
}
