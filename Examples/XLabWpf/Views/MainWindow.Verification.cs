using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;
using System.Windows.Media;
using XLabWpf.Controls;
using XLab.Shared.ViewModels;

namespace XLabWpf.Views;

public partial class MainWindow
{
    internal async Task ClickReferenceAsync(ReferenceItem item)
    {
        Model.SelectedMode = item.Mode;
        Workspace.UpdateLayout();
        var tile = Descendants<ReferenceTile>(Workspace).First(button => ReferenceEquals(button.DataContext, item));
        tile.BringIntoView();
        var peer = UIElementAutomationPeer.CreatePeerForElement(tile)!;
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
        await Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        if (!item.IsSelected || Model.CharacterReferences.Concat(Model.ClothingReferences).Concat(Model.StyleReferences).Count(reference => reference.IsSelected) != 1)
            throw new InvalidOperationException("Clicking the reference button did not select it: " + item.Name);
        await Model.WaitForConditionsAsync().WaitAsync(TimeSpan.FromSeconds(45));
        if (Model.Error.Length > 0) throw new InvalidOperationException(Model.Error);
    }
    internal async Task VerifyUploadClickAsync()
    {
        var tile = Descendants<ReferenceTile>(Workspace).First(button => button.IsVisible && button.DataContext is ReferenceItem { IsUpload: true });
        var peer = UIElementAutomationPeer.CreatePeerForElement(tile)!;
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
        await Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        if (!Model.Error.Contains("Reference uploads require a live session."))
            throw new InvalidOperationException("Clicking Upload did not reach its handler.");
        Model.ClearError();
    }
    internal async Task<object> VerifyPageEntryAsync(bool active, string? output)
    {
        var page = active ? (FrameworkElement)Workspace : Landing;
        await Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        await Task.Delay(80);
        double opacity = page.Opacity, shift = ((TranslateTransform)page.RenderTransform).Y;
        if (SystemParameters.ClientAreaAnimation && !(opacity > 0 && opacity < 1 && shift > 0 && shift < 14))
            throw new InvalidOperationException("The visible page did not fade and slide during entry.");
        if (!string.IsNullOrEmpty(output)) SaveUiScreenshot(System.IO.Path.Combine(output, active ? "transition-generation.png" : "transition-landing.png"));
        await Task.Delay(500);
        if (!page.IsVisible || Math.Abs(page.Opacity - 1) > .001 || Math.Abs(((TranslateTransform)page.RenderTransform).Y) > .001 || PageScroller.VerticalOffset != 0)
            throw new InvalidOperationException("Page entry did not settle at the correct position.");
        if (!active && (!Footer.IsVisible || Footer.Opacity != 1 || ((TranslateTransform)Footer.RenderTransform).Y != 0))
            throw new InvalidOperationException("The landing footer did not finish its entry animation.");
        return new { page = active ? "generation" : "landing", durationMilliseconds = 450, offsetDip = 14,
            animationEnabled = SystemParameters.ClientAreaAnimation, sampledOpacity = opacity, sampledOffset = shift, settled = true };
    }
    private static IEnumerable<T> Descendants<T>(DependencyObject parent) where T : DependencyObject
    {
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(parent); ++i)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is T match) yield return match;
            foreach (var descendant in Descendants<T>(child)) yield return descendant;
        }
    }
}
