using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace XLab.Shared.ViewModels;

internal sealed class ReferenceItem : INotifyPropertyChanged
{
    public string Id { get; init; } = "";
    public string Mode { get; init; } = "charx";
    public string Name { get; init; } = "";
    public string ThumbnailUrl { get; init; } = "";
    public string ReferenceUrl { get; set; } = "";
    public string? LocalPath { get; init; }
    public bool IsUpload { get; init; }
    private bool selected, loading, failed, uploading;
    private object? thumbnail;
    private string error = "";
    public bool IsSelected { get => selected; set { if (selected == value) return; selected = value; Changed(); } }
    public bool IsLoading { get => loading; set { loading = value; Changed(); } }
    public bool HasError { get => failed; set { failed = value; Changed(); } }
    public bool IsUploading { get => uploading; set { uploading = value; Changed(); } }
    public string Error { get => error; set { error = value; Changed(); } }
    public object? Thumbnail { get => thumbnail; set { thumbnail = value; Changed(); } }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
}
