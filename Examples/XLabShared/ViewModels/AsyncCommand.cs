using System.Windows.Input;

namespace XLab.Shared.ViewModels;

internal sealed class AsyncCommand(Func<Task> action, Func<bool>? canExecute = null) : ICommand
{
    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => canExecute?.Invoke() ?? true;
    public async void Execute(object? parameter) { if (CanExecute(parameter)) await action(); }
    internal void Refresh() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
