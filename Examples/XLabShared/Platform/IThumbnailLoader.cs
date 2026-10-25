namespace XLab.Shared.Platform;

internal interface IThumbnailLoader
{
    Task<object> LoadAsync(string source);
}
