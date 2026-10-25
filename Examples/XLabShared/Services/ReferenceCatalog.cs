using System.Text.Json;
using System.Reflection;
using XLab.Shared.ViewModels;

namespace XLab.Shared.Services;

internal static class ReferenceCatalog
{
    internal static IReadOnlyList<ReferenceItem> Load()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("XLab.Shared.presets.json")!;
        using var document = JsonDocument.Parse(stream);
        return document.RootElement.EnumerateArray().Select(item => new ReferenceItem
        {
            Id = item.GetProperty("id").GetString()!, Mode = item.GetProperty("mode").GetString()!,
            Name = item.GetProperty("name").GetString()!, ThumbnailUrl = item.GetProperty("thumbnail").GetString()!,
            ReferenceUrl = item.GetProperty("reference").GetString()!
        }).ToList();
    }
}
