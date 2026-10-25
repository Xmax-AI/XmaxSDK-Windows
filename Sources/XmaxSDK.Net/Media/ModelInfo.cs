//
//  ModelInfo.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The supported formats, defaults, and input limits of a generation model.
/// </summary>
/// <param name="Identifier">The model identifier accepted by realtime configuration.</param>
/// <param name="DisplayName">The model display name.</param>
/// <param name="SupportedFormats">The model-supported generation canvas sizes with their default frame rates.</param>
/// <param name="DefaultCameraFormat">The default camera encoding target, which does not force the capture dimensions.</param>
/// <param name="MinimumInputPixels">The model minimum input-pixel metadata, expressed as width multiplied by height.</param>
/// <param name="MaximumInputPixels">The maximum input-pixel count, expressed as width multiplied by height.</param>
/// <param name="InputSizeAlignment">The canvas dimension alignment used when the model has no fixed format list.</param>
public sealed record ModelInfo(
    string Identifier,
    string DisplayName,
    IReadOnlyList<VideoFormat> SupportedFormats,
    VideoFormat DefaultCameraFormat,
    uint MinimumInputPixels,
    uint MaximumInputPixels,
    uint InputSizeAlignment
);
