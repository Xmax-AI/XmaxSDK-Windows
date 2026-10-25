using Microsoft.UI.Xaml;
using System.IO;
using XLab.Shared.Services;

namespace XLabWinUI;

public partial class App : Application
{
    private Window? window;
    public App()
    {
        InitializeComponent();
        UnhandledException += (_, args) =>
        {
            string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
            if (!string.IsNullOrEmpty(output))
            { Directory.CreateDirectory(output); File.WriteAllText(Path.Combine(output, "unhandled.txt"), args.Exception.ToString()); }
        };
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        string[] arguments = Environment.GetCommandLineArgs();
        string? mode = arguments.FirstOrDefault(value => value.StartsWith("--smoke-test"));
        bool preview = arguments.Contains("--preview") || arguments.Contains("--preview-session") || mode is "--smoke-test-ui" or "--smoke-test-frames" or "--smoke-test-reference-close";
        ApiKeyStore? store = !preview && mode is null ? new() : null;
        if (mode?.StartsWith("--smoke-test-key-memory-") == true)
            store = new ApiKeyStore("XMAX.AI/XLabWinUI/ApiKey.Smoke." + Guid.Parse(Environment.GetEnvironmentVariable("XMAX_SMOKE_CREDENTIAL_ID") ?? "").ToString("N"));
        var main = new MainWindow(preview, arguments.Contains("--preview-session") || mode is "--smoke-test-frames" or "--smoke-test-reference-close", store, mode is null or "--smoke-test-camera" or "--smoke-test-live");
        if (mode is not null) main.Ready += async () => await main.RunSmokeAsync(mode);
        window = main;
        window.Activate();
    }
}
