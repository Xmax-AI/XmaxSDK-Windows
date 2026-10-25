//
//  GenerationContext.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The prompt, reference image, and face-restoration conditions for generation.
/// </summary>
/// <param name="Prompt">The generation instruction sent to the service.</param>
/// <param name="ReferenceImage">The service-supported reference image path or uploaded URL; null omits the reference condition.</param>
/// <param name="FaceRestoration">Whether to enable the service face-restoration option.</param>
public sealed record GenerationContext(
    string Prompt,
    string? ReferenceImage = null,
    bool FaceRestoration = false
);
