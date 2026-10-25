using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace XLabWpf.Controls;

public sealed class FalseVisibleConverter : IValueConverter
{
    public object Convert(object value, Type target, object parameter, CultureInfo culture) => value is true ? Visibility.Collapsed : Visibility.Visible;
    public object ConvertBack(object value, Type target, object parameter, CultureInfo culture) => Binding.DoNothing;
}
public sealed class ModeVisibleConverter : IValueConverter
{
    public object Convert(object value, Type target, object parameter, CultureInfo culture) => Equals(value, parameter) ? Visibility.Visible : Visibility.Collapsed;
    public object ConvertBack(object value, Type target, object parameter, CultureInfo culture) => Binding.DoNothing;
}
public sealed class ModeCheckedConverter : IValueConverter
{
    public object Convert(object value, Type target, object parameter, CultureInfo culture) => Equals(value, parameter);
    public object ConvertBack(object value, Type target, object parameter, CultureInfo culture) => value is true ? parameter : Binding.DoNothing;
}
public sealed class CapsuleRadiusConverter : IValueConverter
{
    public object Convert(object value, Type target, object parameter, CultureInfo culture) => new CornerRadius(value is double height ? height / 2 : 18);
    public object ConvertBack(object value, Type target, object parameter, CultureInfo culture) => Binding.DoNothing;
}
