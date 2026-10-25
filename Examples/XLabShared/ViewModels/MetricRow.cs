using System.ComponentModel;

namespace XLab.Shared.ViewModels;

internal sealed class MetricRow(string label) : INotifyPropertyChanged
{
    public string Label { get; } = label;
    private string value = "—";
    public string Value { get => value; set { this.value = value; PropertyChanged?.Invoke(this, new(nameof(Value))); } }
    public event PropertyChangedEventHandler? PropertyChanged;
}
