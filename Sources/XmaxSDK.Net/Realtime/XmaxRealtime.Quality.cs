//
//  XmaxRealtime.Quality.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{
    /// <summary>
    /// Applies an uplink quality target during generation.
    /// </summary>
    /// <param name="level">The transport quality target.</param>
    /// <returns>A task completing when the target has been applied.</returns>
    public Task SetUplinkQualityLevelAsync(
        RealtimeQualityLevel level
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_uplink_quality_level(value, (int)level)));

    /// <summary>
    /// Reads the applied uplink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    public Task<RealtimeQualityLevel?> GetUplinkQualityLevelAsync() => operations.Run(value =>
    {
        int level = GetQuality(value).uplink_level;
        return level == 0 ? (RealtimeQualityLevel?)null : (RealtimeQualityLevel)level;
    });

    /// <summary>Applies a downlink quality target during generation.</summary>
    public Task SetDownlinkQualityLevelAsync(RealtimeQualityLevel level) => Run(value =>
    {
        NativeError.Check(NativeMethods.xmax_realtime_set_downlink_quality_level(value, (int)level));
        RefreshResultTrackFormat(value);
    });

    /// <summary>
    /// Reads the applied downlink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    public Task<RealtimeQualityLevel?> GetDownlinkQualityLevelAsync() => operations.Run(value =>
    {
        int level = GetQuality(value).downlink_level;
        return level == 0 ? (RealtimeQualityLevel?)null : (RealtimeQualityLevel)level;
    });

    private static unsafe xmax_quality_status GetQuality(nint value)
    {
        var native = new xmax_quality_status { struct_size = (uint)sizeof(xmax_quality_status) };
        NativeError.Check(NativeMethods.xmax_realtime_get_quality(value, &native));
        return native;
    }
}
