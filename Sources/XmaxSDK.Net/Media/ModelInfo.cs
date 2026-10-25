namespace Xmax.SDK;

public sealed record ModelInfo(string Identifier, string DisplayName, IReadOnlyList<VideoFormat> SupportedFormats,
    VideoFormat DefaultCameraFormat, uint MinimumInputPixels, uint MaximumInputPixels, uint InputSizeAlignment);
