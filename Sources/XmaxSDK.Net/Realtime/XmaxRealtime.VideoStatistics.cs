//
//  XmaxRealtime.VideoStatistics.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

public sealed partial class XmaxRealtime
{
    /// <summary>Reads the latest uplink measurements, or null before publication or after disconnection.</summary>
    public UplinkVideoStatistics? GetUplinkVideoStatistics()
    {
        var value = ReadVideoStatistics(0);
        return value.available == 0 ? null : new(
            PositiveDimension(value.width), PositiveDimension(value.height),
            AvailableMeasurement(value.capture_fps), AvailableMeasurement(value.encode_fps),
            AvailableMeasurement(value.send_fps), AvailableMeasurement(value.bitrate_kbps),
            PacketLoss(value.packet_loss_percent)
        );
    }

    /// <summary>Reads the latest downlink measurements, or null when no result statistics are available.</summary>
    public unsafe DownlinkVideoStatistics? GetDownlinkVideoStatistics()
    {
        var value = ReadVideoStatistics(1);
        return value.available == 0 ? null : new(
            NativeError.Text(value.user_id), PositiveDimension(value.width), PositiveDimension(value.height),
            AvailableMeasurement(value.decode_fps), AvailableMeasurement(value.render_fps),
            AvailableMeasurement(value.bitrate_kbps), PacketLoss(value.packet_loss_percent),
            AvailableMeasurement(value.e2e_ms)
        );
    }

    private unsafe xmax_video_statistics ReadVideoStatistics(int direction)
    {
        using var lease = handle.Borrow();
        var value = new xmax_video_statistics { struct_size = (uint)sizeof(xmax_video_statistics) };
        NativeError.Check(NativeMethods.xmax_realtime_get_video_statistics(lease.Value, direction, &value));
        return value;
    }

    private static int? PositiveDimension(int value) => value > 0 ? value : null;
    private static int? AvailableMeasurement(int value) => value < 0 ? null : value;
    private static int? PacketLoss(int value) => value is >= 0 and <= 100 ? value : null;
}
