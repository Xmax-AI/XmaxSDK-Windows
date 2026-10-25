namespace Xmax.SDK;

public readonly record struct MediaTime(long Value, int Timescale)
{ public double? Seconds => Timescale > 0 ? (double)Value / Timescale : null; }
