//
//  VideoFormat.cs
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
/// <param name="FramesPerSecond">The nominal frame rate, in frames per second.</param>
/// <remarks>
/// Camera frames are center-cropped and scaled to the configured dimensions for preview and publication.
/// External input frames must match their bound format. Query model capabilities before selecting a camera target.
/// </remarks>
public readonly record struct VideoFormat(
    uint Width,
    uint Height,
    uint FramesPerSecond = 30
)
{
    /// <summary>Gets the minimum uplink bitrate in kbps; null uses the SDK default and zero removes the limit.</summary>
    public int? MinimumBitrateKbps { get; init; }

    /// <summary>Gets the optional maximum uplink bitrate in kbps.</summary>
    public int? MaximumBitrateKbps { get; init; }

    /// <summary>Gets the uplink encoder's adaptation preference.</summary>
    public EncodingPreference EncoderPreference { get; init; } = EncodingPreference.MaintainFramerate;

    /// <summary>
    /// Creates a video format with uplink encoder options.
    /// </summary>
    /// <param name="width">The width, in pixels.</param>
    /// <param name="height">The height, in pixels.</param>
    /// <param name="framesPerSecond">The nominal frame rate.</param>
    /// <param name="minimumBitrateKbps">The minimum in kbps; null uses the SDK default and zero removes the limit.</param>
    /// <param name="maximumBitrateKbps">A positive maximum in kbps, or null for the SDK default.</param>
    /// <param name="encoderPreference">The encoder's quality adaptation preference.</param>
    public VideoFormat(
        uint width,
        uint height,
        uint framesPerSecond,
        int? minimumBitrateKbps,
        int? maximumBitrateKbps,
        EncodingPreference encoderPreference = EncodingPreference.MaintainFramerate
    ) : this(
        width,
        height,
        framesPerSecond
    )
    {
        MinimumBitrateKbps = minimumBitrateKbps;
        MaximumBitrateKbps = maximumBitrateKbps;
        EncoderPreference = encoderPreference;
    }

    internal unsafe xmax_encoding_config NativeEncoding
    {
        get
        {
            if (MinimumBitrateKbps is < 0)
                throw new ArgumentOutOfRangeException(nameof(MinimumBitrateKbps));
            if (MaximumBitrateKbps is <= 0)
                throw new ArgumentOutOfRangeException(nameof(MaximumBitrateKbps));
            return new()
            {
                struct_size = (uint)sizeof(xmax_encoding_config),
                minimum_bitrate_kbps = MinimumBitrateKbps ?? -1,
                maximum_bitrate_kbps = MaximumBitrateKbps ?? 0,
                preference = (int)(this == default ? EncodingPreference.MaintainFramerate : EncoderPreference)
            };
        }
    }

    internal VideoFormat WithEncoding(xmax_encoding_config encoding) => this with
    {
        MinimumBitrateKbps = encoding.minimum_bitrate_kbps >= 0 ? encoding.minimum_bitrate_kbps : null,
        MaximumBitrateKbps = encoding.maximum_bitrate_kbps > 0 ? encoding.maximum_bitrate_kbps : null,
        EncoderPreference = (EncodingPreference)encoding.preference
    };

    internal xmax_video_format Native => new() { width = Width, height = Height, fps = FramesPerSecond };

    internal static VideoFormat From(xmax_video_format value) => new(value.width, value.height, value.fps);

    /// <summary>
    /// Formats dimensions and nominal frame rate for display.
    /// </summary>
    /// <returns>A representation in pixels and frames per second.</returns>
    public override string ToString() => $"{Width} × {Height} @ {FramesPerSecond} fps";
}
