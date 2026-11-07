//
//  BeautyPreferencesStore.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.IO;
using System.Text.Json;
using Xmax.SDK;

namespace XLab.Shared.Services;

internal sealed record BeautyPreferences(bool Enabled, RealtimeVideoBeautyOptions Options)
{
    internal static BeautyPreferences Default => new(true, new());
}

internal sealed class BeautyPreferencesStore(string? path = null)
{
    private readonly string path = path ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "XMAX.AI", System.Reflection.Assembly.GetEntryAssembly()?.GetName().Name ?? "XLab", "beauty.json");
    internal BeautyPreferences Load()
    {
        try {
            var value = JsonSerializer.Deserialize<BeautyPreferences>(File.ReadAllText(path));
            if (value?.Options is null) return BeautyPreferences.Default;
            value.Options.Validate();
            return value;
        } catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException or XmaxException) {
            return BeautyPreferences.Default;
        }
    }
    internal void Save(BeautyPreferences value)
    {
        string temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(temporary, JsonSerializer.Serialize(value));
            File.Move(temporary, path, true);
        } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        finally { try { if (File.Exists(temporary)) File.Delete(temporary); } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { } }
    }
}
