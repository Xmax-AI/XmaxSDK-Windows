using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Xmax.SDK.Wpf.Rendering;

internal sealed class WriteableBitmapPresenter
{
    internal WriteableBitmap? Bitmap { get; private set; }
    internal void Present(XmaxVideoFrame frame)
    {
        int width = checked((int)frame.Width), height = checked((int)frame.Height);
        if (Bitmap is null || Bitmap.PixelWidth != width || Bitmap.PixelHeight != height)
            Bitmap = new(width, height, 96, 96, PixelFormats.Bgra32, null);
        // Native pixels stay leased throughout this synchronous UI-thread copy. No intermediate byte[].
        frame.ReadBgra((data, stride, size) => Bitmap.WritePixels(new Int32Rect(0, 0, width, height), data, size, stride));
    }
    internal void Clear() => Bitmap = null;
}
