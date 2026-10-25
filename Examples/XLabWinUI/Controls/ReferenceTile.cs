using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using XLab.Shared.ViewModels;
using XLabWinUI.Platform;

namespace XLabWinUI.Controls;

internal sealed class ReferenceTile : Button
{
    internal ReferenceItem Item { get; }
    private readonly Border circle;
    private readonly Image image;
    private readonly TextBlock name, state;
    private bool hot;
    internal ReferenceTile(ReferenceItem item)
    {
        Item = item; DataContext = item;
        Width = Height = 84; Padding = new(0); BorderThickness = new(0); Background = Theme.Brush("#00FFFFFF"); Template = Theme.ButtonTemplate;
        HorizontalContentAlignment = HorizontalAlignment.Stretch; VerticalContentAlignment = VerticalAlignment.Stretch;
        AutomationProperties.SetName(this, item.Name);
        AutomationProperties.SetAutomationId(this, item.Id.Length > 0 ? "reference_" + item.Id : "upload_" + item.Mode);
        var root = new Grid();
        root.RowDefinitions.Add(new() { Height = new(64) }); root.RowDefinitions.Add(new() { Height = new(6) }); root.RowDefinitions.Add(new() { Height = new(14) });
        image = new() { Stretch = Stretch.UniformToFill };
        circle = new() { Width = 64, Height = 64, CornerRadius = new(32), BorderThickness = new(item.IsUpload ? 1 : 2), Background = Theme.Brush("#F7F8FA"), Child = image, HorizontalAlignment = HorizontalAlignment.Center };
        RoundedClip.Attach(circle, 32);
        root.Children.Add(circle);
        if (item.IsUpload) root.Children.Add(new Glyph { Kind = "plus", Width = 22, Height = 22, Foreground = Theme.Brush("#6B7380"), HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
        state = Theme.Text("", 11, "#6B7380"); state.HorizontalAlignment = HorizontalAlignment.Center;
        root.Children.Add(state);
        name = Theme.Text(item.Name, 11, "#6B7380"); name.TextAlignment = TextAlignment.Center; name.TextTrimming = TextTrimming.CharacterEllipsis; Grid.SetRow(name, 2); root.Children.Add(name);
        Content = root;
        Click += RetryThumbnail;
        item.PropertyChanged += (_, _) => Refresh();
        Loaded += async (_, _) => await LoadAsync();
        PointerEntered += (_, _) => { hot = true; Refresh(); };
        PointerExited += (_, _) => { hot = false; Refresh(); };
        Refresh();
    }
    private void RetryThumbnail(object sender, RoutedEventArgs args)
    {
        if (Item.HasError && Item.Thumbnail is null) { Item.HasError = false; _ = LoadAsync(); }
    }
    private async Task LoadAsync()
    {
        if (Item.IsUpload || Item.Thumbnail is not null || Item.IsLoading || Item.HasError) return;
        Item.IsLoading = true;
        try { Item.Thumbnail = await ThumbnailCache.LoadAsync(Item.ThumbnailUrl); Item.HasError = false; }
        catch (Exception error) { Item.HasError = true; Item.Error = "Unable to load thumbnail — click to retry. " + error.Message; }
        finally { Item.IsLoading = false; }
    }
    internal void Refresh()
    {
        circle.BorderBrush = Theme.Brush(Item.IsSelected ? "#2F46E2" : hot ? "#C7D0F8" : "#F3F4F6");
        name.Foreground = Theme.Brush(Item.IsSelected ? "#2F46E2" : "#6B7380");
        image.Source = Item.Thumbnail as ImageSource;
        state.Text = Item.IsUploading ? "Uploading…" : Item.IsLoading ? "···" : Item.HasError ? "Retry" : "";
        ToolTipService.SetToolTip(this, Item.HasError ? Item.Error : Item.Name);
        IsEnabled = !Item.IsUploading;
    }
}
