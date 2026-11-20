//
//  RealtimeVideoFormat.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>
/// The video dimensions and nominal frame rate for media input or encoding.
/// </summary>
/// <param name="Width">The width, in pixels.</param>
/// <param name="Height">The height, in pixels.</param>
/// <param name="Fps">The nominal frame rate, in frames per second.</param>
/// <remarks>
/// Camera frames are center-cropped and scaled to the configured dimensions for preview and publication.
/// External input frames must match their bound format. Query model capabilities before selecting a camera target.
/// </remarks>
public readonly record struct RealtimeVideoFormat(
    uint Width,
    uint Height,
    uint Fps = 30
)
{
    /// <summary>Gets the minimum uplink bitrate in kbps; null uses the SDK default and zero removes the limit.</summary>
    public int? MinimumBitrate { get; init; }

    /// <summary>Gets the optional maximum uplink bitrate in kbps.</summary>
    public int? MaximumBitrate { get; init; }

    /// <summary>Gets the uplink encoder's adaptation preference.</summary>
    public RealtimeVideoEncoderPreference EncoderPreference { get; init; } = RealtimeVideoEncoderPreference.MaintainFramerate;

    /// <summary>
    /// Creates a video format with uplink encoder options.
    /// </summary>
    /// <param name="width">The width, in pixels.</param>
    /// <param name="height">The height, in pixels.</param>
    /// <param name="fps">The nominal frame rate.</param>
    /// <param name="minimumBitrate">The minimum in kbps; null uses the SDK default and zero removes the limit.</param>
    /// <param name="maximumBitrate">A positive maximum in kbps, or null for the SDK default.</param>
    /// <param name="encoderPreference">The encoder's quality adaptation preference.</param>
    public RealtimeVideoFormat(
        uint width,
        uint height,
        uint fps,
        int? minimumBitrate,
        int? maximumBitrate,
        RealtimeVideoEncoderPreference encoderPreference = RealtimeVideoEncoderPreference.MaintainFramerate
    ) : this(
        width,
        height,
        fps
    )
    {
        MinimumBitrate = minimumBitrate;
        MaximumBitrate = maximumBitrate;
        EncoderPreference = encoderPreference;
    }

    internal unsafe xmax_encoding_config NativeEncoding
    {
        get
        {
            if (MinimumBitrate is < 0)
                throw new ArgumentOutOfRangeException(nameof(MinimumBitrate));
            if (MaximumBitrate is <= 0)
                throw new ArgumentOutOfRangeException(nameof(MaximumBitrate));
            return new()
            {
                struct_size = (uint)sizeof(xmax_encoding_config),
                minimum_bitrate_kbps = MinimumBitrate ?? -1,
                maximum_bitrate_kbps = MaximumBitrate ?? 0,
                preference = (int)(this == default ? RealtimeVideoEncoderPreference.MaintainFramerate : EncoderPreference)
            };
        }
    }

    internal RealtimeVideoFormat WithEncoding(xmax_encoding_config encoding) => this with
    {
        MinimumBitrate = encoding.minimum_bitrate_kbps >= 0 ? encoding.minimum_bitrate_kbps : null,
        MaximumBitrate = encoding.maximum_bitrate_kbps > 0 ? encoding.maximum_bitrate_kbps : null,
        EncoderPreference = (RealtimeVideoEncoderPreference)encoding.preference
    };

    internal xmax_video_format Native => new() { width = Width, height = Height, fps = Fps };

    internal static RealtimeVideoFormat From(xmax_video_format value) => new(value.width, value.height, value.fps);

    /// <summary>
    /// Formats dimensions and nominal frame rate for display.
    /// </summary>
    /// <returns>A representation in pixels and frames per second.</returns>
    public override string ToString() => $"{Width} × {Height} @ {Fps} fps";
}
