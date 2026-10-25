using System.Windows;
using System.Windows.Controls;
using XLabWpf.Services;
using XLab.Shared.ViewModels;

namespace XLabWpf.Controls;

public sealed class ReferenceTile : Button
{
    public ReferenceTile()
    {
        Loaded += (_, _) => LoadThumbnail();
        IsVisibleChanged += (_, _) => { if (IsVisible) LoadThumbnail(); };
        DataContextChanged += (_, _) => LoadThumbnail();
    }
    protected override void OnClick()
    {
        if (DataContext is ReferenceItem { HasError: true, Thumbnail: null } item) { item.HasError = false; LoadThumbnail(); }
        base.OnClick();
    }
    private async void LoadThumbnail()
    {
        if (!IsVisible || DataContext is not ReferenceItem item || item.IsUpload || item.IsLoading || item.Thumbnail is not null || item.HasError) return;
        item.IsLoading = true;
        try { item.Thumbnail = await ThumbnailCache.LoadAsync(item.ThumbnailUrl); item.HasError = false; }
        catch (Exception error) { item.HasError = true; item.Error = "Unable to load thumbnail — click to retry. " + error.Message; }
        finally { item.IsLoading = false; }
    }
}
