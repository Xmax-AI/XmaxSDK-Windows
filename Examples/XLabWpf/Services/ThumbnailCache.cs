using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Media.Imaging;

namespace XLabWpf.Services;

internal static class ThumbnailCache
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(20) };
    private static readonly SemaphoreSlim Slots = new(6);
    internal static async Task<BitmapSource> LoadAsync(string source)
    {
        await Slots.WaitAsync().ConfigureAwait(false);
        try
        {
            byte[] bytes;
            if (File.Exists(source)) bytes = await File.ReadAllBytesAsync(source).ConfigureAwait(false);
            else
            {
                // The same catalog image, requested as PNG because WPF does not bundle a WebP codec.
                string url = source.Replace("/format/webp", "/format/png", StringComparison.Ordinal);
                var uri = new Uri(url);
                if (uri.Scheme != "https") throw new InvalidOperationException("Thumbnail URL must use HTTPS.");
                string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Xmax", "XLabWpf", "Thumbnails");
                string file = Path.Combine(directory, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(url))) + ".png");
                if (File.Exists(file))
                {
                    try { return Decode(await File.ReadAllBytesAsync(file).ConfigureAwait(false)); }
                    catch (Exception error) when (error is IOException or NotSupportedException or FileFormatException or ArgumentException)
                    { try { File.Delete(file); } catch (IOException) { } }
                }
                {
                    using var response = await Http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead).ConfigureAwait(false);
                    response.EnsureSuccessStatusCode();
                    if (response.Content.Headers.ContentLength > 8 * 1024 * 1024) throw new InvalidOperationException("Thumbnail is too large.");
                    using var content = await response.Content.ReadAsStreamAsync().ConfigureAwait(false);
                    using var memory = new MemoryStream();
                    byte[] buffer = new byte[16384];
                    int read;
                    while ((read = await content.ReadAsync(buffer).ConfigureAwait(false)) > 0)
                    { if (memory.Length + read > 8 * 1024 * 1024) throw new InvalidOperationException("Thumbnail is too large."); await memory.WriteAsync(buffer.AsMemory(0, read)).ConfigureAwait(false); }
                    bytes = memory.ToArray();
                    var decoded = Decode(bytes);
                    Directory.CreateDirectory(directory);
                    string temporary = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
                    try { await File.WriteAllBytesAsync(temporary, bytes).ConfigureAwait(false); File.Move(temporary, file, true); }
                    catch (IOException) { if (File.Exists(temporary)) File.Delete(temporary); }
                    return decoded;
                }
            }
            return Decode(bytes);
        }
        finally { Slots.Release(); }
    }
    private static BitmapSource Decode(byte[] bytes)
    {
        using var stream = new MemoryStream(bytes);
        var image = new BitmapImage();
        image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.DecodePixelWidth = 160; image.StreamSource = stream; image.EndInit(); image.Freeze();
        return image;
    }
}
