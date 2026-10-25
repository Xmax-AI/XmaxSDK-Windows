namespace Xmax.SDK;

public sealed record QualityStatus(bool AdaptiveEnabled, int UplinkLevel, int DownlinkLevel, VideoFormat Uplink, VideoFormat Downlink);
