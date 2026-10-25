namespace Xmax.SDK;

public sealed record UploadOptions(string? FileName = null, string? ContentType = null, bool CheckImageSafety = true);
