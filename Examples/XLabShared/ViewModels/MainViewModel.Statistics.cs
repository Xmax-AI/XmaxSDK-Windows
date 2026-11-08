//
//  MainViewModel.Statistics.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel
{
    private void ReceiveUplinkStatistics(UplinkVideoStatistics? stats)
    {
        int? fps = stats?.SendFrameRate;
        UpdateVideoRows(UplinkRows, stats?.Width, stats?.Height, fps, stats?.BitrateKbps);
        UplinkNetworkRows[0].Value = FormatMetric(stats?.PacketLossPercent, "%");
        UpdateVideoSummary(false, stats?.Width, stats?.Height, fps, stats?.BitrateKbps, null);
    }

    private void ReceiveDownlinkStatistics(DownlinkVideoStatistics? stats)
    {
        int? fps = stats?.RenderFrameRate ?? stats?.DecodeFrameRate;
        UpdateVideoRows(DownlinkRows, stats?.Width, stats?.Height, fps, stats?.BitrateKbps);
        DownlinkNetworkRows[1].Value = FormatMetric(stats?.PacketLossPercent, "%");
        DownlinkNetworkRows[4].Value = FormatMetric(stats?.EndToEndDelayMs, " ms");
        UpdateVideoSummary(true, stats?.Width, stats?.Height, fps, stats?.BitrateKbps, stats?.EndToEndDelayMs);
    }

    private static void UpdateVideoRows(MetricRow[] rows, int? width, int? height, int? fps, int? bitrate)
    {
        rows[0].Value = width is > 0 && height is > 0 ? $"{width}×{height}" : "—";
        rows[1].Value = FormatMetric(fps, " fps");
        rows[2].Value = FormatMetric(bitrate, " kbps");
    }

    private static string FormatMetric(int? value, string unit = "") => value is { } measured ? $"{measured}{unit}" : "—";

    private void UpdateVideoSummary(bool downlink, int? width, int? height, int? fps, int? bitrate, int? delay)
    {
        if (width is not > 0 || height is not > 0) return;
        Metrics = $"{(downlink ? "下行" : "上行")}  {width} × {height}  ·  {FormatMetric(fps)} fps  ·  {FormatMetric(bitrate)} kbps  ·  延迟 {FormatMetric(delay)} ms";
    }

    private void ReceiveLaunchTiming(RealtimeLaunchTiming timing)
    {
        double?[] values = [timing.SessionMilliseconds, timing.RtcJoinMilliseconds, timing.PublishMilliseconds,
            timing.FirstFrameMilliseconds, timing.TotalMilliseconds];
        for (int i = 0; i < values.Length; ++i)
            TimingRows[i].Value = values[i] is { } milliseconds ? $"{milliseconds:F1} ms" : "—";
        if (timing.FirstFrameMilliseconds is { } firstFrame && timing.TotalMilliseconds is { } total)
            Metrics = $"首帧 {firstFrame:F0} ms  ·  总启动 {total:F0} ms";
    }

    private void ReceiveNetworkQuality(RealtimeNetworkQuality quality)
    {
        UplinkNetworkRows[1].Value = FormatNetworkQuality(quality.Uplink);
        DownlinkNetworkRows[2].Value = FormatNetworkQuality(quality.Downlink);
    }

    private static string FormatNetworkQuality(RealtimeNetworkQualityLevel quality) =>
        quality == RealtimeNetworkQualityLevel.Unknown ? "—" : ((int)quality).ToString();
}
