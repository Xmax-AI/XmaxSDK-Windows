namespace XLab.Shared.Platform;

internal interface IUiDispatcher
{
    void InvokeAsync(Action action);
    IUiTimer CreateTimer(TimeSpan interval);
}
internal interface IUiTimer
{
    bool IsEnabled { get; }
    event EventHandler? Tick;
    void Start();
    void Stop();
}
