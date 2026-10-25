using System.Windows.Threading;
using XLab.Shared.Platform;

namespace XLabWpf.Services;

internal sealed class WpfUiDispatcher(Dispatcher dispatcher) : IUiDispatcher
{
    public void InvokeAsync(Action action) => dispatcher.InvokeAsync(action);
    public IUiTimer CreateTimer(TimeSpan interval) => new WpfUiTimer(interval);
}
internal sealed class WpfUiTimer(TimeSpan interval) : IUiTimer
{
    private readonly DispatcherTimer timer = new() { Interval = interval };
    public bool IsEnabled => timer.IsEnabled;
    public event EventHandler? Tick { add => timer.Tick += value; remove => timer.Tick -= value; }
    public void Start() => timer.Start();
    public void Stop() => timer.Stop();
}
internal sealed class WpfThumbnailLoader : IThumbnailLoader
{
    public async Task<object> LoadAsync(string source) => await ThumbnailCache.LoadAsync(source);
}
