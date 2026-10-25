using System.IO;
using System.Security;
using Microsoft.Win32;

namespace XLab.Shared.Services;

internal static class CameraSelectionStore
{
    private const string KeyPath = @"Software\XMAX.AI\XLabWpf";
    internal static string? Load()
    {
        try { using var key = Registry.CurrentUser.OpenSubKey(KeyPath); return key?.GetValue("CameraId") as string; }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or SecurityException) { return null; }
    }
    internal static void Save(string? id)
    {
        try
        {
            using var key = Registry.CurrentUser.CreateSubKey(KeyPath);
            if (id is null) key.DeleteValue("CameraId", false);
            else key.SetValue("CameraId", id);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or SecurityException) { }
    }
}
