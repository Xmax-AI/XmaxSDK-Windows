namespace Xmax.SDK;

public sealed record GenerationContext(string Prompt, string? ReferenceImage = null, bool FaceRestoration = false);
