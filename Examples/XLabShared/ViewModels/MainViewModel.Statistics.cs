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

    private void UpdateMetricRows(RealtimeEvent item)
    {
        if (item.Type == RealtimeEventType.Timing)
        {
            double[] values = [item.SessionMilliseconds, item.JoinMilliseconds, item.PublishMilliseconds, item.FirstFrameMilliseconds, item.TotalMilliseconds];
            for (int i = 0; i < values.Length; ++i) TimingRows[i].Value = values[i] < 0 ? "—" : $"{values[i]:F1} ms";
        }
        if (item.Type != RealtimeEventType.Statistics) return;
        var network = item.Direction == 0 ? UplinkNetworkRows : DownlinkNetworkRows;
        if (item.Quality >= 0) network[item.Direction == 0 ? 1 : 2].Value = item.Quality.ToString();
    }
}
