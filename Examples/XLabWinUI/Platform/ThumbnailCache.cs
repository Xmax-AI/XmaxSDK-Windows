using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage.Streams;
using XLab.Shared.Platform;

namespace XLabWinUI.Platform;

internal sealed class WinUiThumbnailLoader : IThumbnailLoader
{
    public async Task<object> LoadAsync(string source) => await ThumbnailCache.LoadAsync(source);
}
internal static class ThumbnailCache
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(20) };
    private static readonly SemaphoreSlim Slots = new(6);
    internal static async Task<BitmapImage> LoadAsync(string source)
    {
        await Slots.WaitAsync();
        try
        {
            byte[] bytes;
            if (File.Exists(source)) bytes = await File.ReadAllBytesAsync(source);
            else
            {
                string url = source.Replace("/format/webp", "/format/png", StringComparison.Ordinal);
                if (new Uri(url).Scheme != "https") throw new InvalidOperationException("Thumbnail URL must use HTTPS.");
                string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Xmax", "XLabWpf", "Thumbnails");
                string file = Path.Combine(directory, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(url))) + ".png");
                if (File.Exists(file))
                {
                    try { return await DecodeAsync(await File.ReadAllBytesAsync(file)); }
                    catch (Exception) { try { File.Delete(file); } catch (IOException) { } }
                }
                using var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
                response.EnsureSuccessStatusCode();
                if (response.Content.Headers.ContentLength > 8 * 1024 * 1024) throw new InvalidOperationException("Thumbnail is too large.");
                using var content = await response.Content.ReadAsStreamAsync();
                using var memory = new MemoryStream();
                byte[] buffer = new byte[16384];
                int read;
                while ((read = await content.ReadAsync(buffer)) > 0)
                { if (memory.Length + read > 8 * 1024 * 1024) throw new InvalidOperationException("Thumbnail is too large."); await memory.WriteAsync(buffer.AsMemory(0, read)); }
                bytes = memory.ToArray();
                Directory.CreateDirectory(directory);
                string temporary = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
                try { await File.WriteAllBytesAsync(temporary, bytes); File.Move(temporary, file, true); }
                catch (IOException) { if (File.Exists(temporary)) File.Delete(temporary); }
            }
            return await DecodeAsync(bytes);
        }
        finally { Slots.Release(); }
    }
    private static async Task<BitmapImage> DecodeAsync(byte[] bytes)
    {
        using var stream = new InMemoryRandomAccessStream();
        using (var writer = new DataWriter(stream.GetOutputStreamAt(0))) { writer.WriteBytes(bytes); await writer.StoreAsync(); }
        stream.Seek(0);
        var image = new BitmapImage { DecodePixelWidth = 160 };
        await image.SetSourceAsync(stream);
        return image;
    }
}
