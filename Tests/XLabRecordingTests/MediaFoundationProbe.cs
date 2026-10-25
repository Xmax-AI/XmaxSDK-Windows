using System.Runtime.InteropServices;
using XLab.Shared.Recording;

/// <summary>Decodes the saved MP4 through a source reader, independently of the encoding path.</summary>
internal static unsafe class MediaFoundationProbe
{
    private static nint Slot(nint value, int index) => ((nint*)*(nint*)value)[index];
    private static void Check(int status) { if (status < 0) Marshal.ThrowExceptionForHR(status); }
    private static void Release(ref nint value) { if (value != 0) Marshal.Release(value); value = 0; }
    private static readonly Guid Major = new("48eba18e-f8c9-4687-bf11-0a74c9f96a8f"), Subtype = new("f7e34c9a-42e8-4714-b74b-cb29d72c35e5");
    private static void Id(nint type, Guid key, Guid value) => Check(((delegate* unmanaged[Stdcall]<nint, Guid*, Guid*, int>)Slot(type, 24))(type, &key, &value));
    internal static void Verify(string path, int width, int height, bool audio)
    {
        // The test entry point can resume on a pool MTA after asynchronous recording completion.
        Check(CoInitializeEx(0, 0)); Check(MFStartup(0x20070, 0));
        try
        {
            Read(path, 0xfffffffc, width, height, false);
            if (audio) Read(path, 0xfffffffd, 0, 0, true);
        }
        finally { MFShutdown(); CoUninitialize(); }
    }
    private static void Read(string path, uint stream, int width, int height, bool audio)
    {
        nint attributes = 0, reader = 0, type = 0;
        try
        {
            Check(MFCreateAttributes(out attributes, 1));
            Guid process = new("fb394f3d-ccf1-42ee-bbb3-f9b845d5681d");
            Check(((delegate* unmanaged[Stdcall]<nint, Guid*, uint, int>)Slot(attributes, 21))(attributes, &process, 1));
            Check(MFCreateSourceReaderFromURL(path, attributes, out reader));
            Check(((delegate* unmanaged[Stdcall]<nint, uint, int, int>)Slot(reader, 4))(reader, 0xfffffffe, 0));
            Check(((delegate* unmanaged[Stdcall]<nint, uint, int, int>)Slot(reader, 4))(reader, stream, 1));
            Check(((delegate* unmanaged[Stdcall]<nint, uint, uint, nint*, int>)Slot(reader, 5))(reader, stream, 0, &type));
            Guid subtypeKey = Subtype, codec = default;
            Check(((delegate* unmanaged[Stdcall]<nint, Guid*, Guid*, int>)Slot(type, 10))(type, &subtypeKey, &codec));
            Program.Check(codec == new Guid(audio ? "00001610-0000-0010-8000-00aa00389b71" : "34363248-0000-0010-8000-00aa00389b71"), "The MP4 codec is not H.264/AAC.");
            Release(ref type); Check(MFCreateMediaType(out type));
            Id(type, Major, new(audio ? "73647561-0000-0010-8000-00aa00389b71" : "73646976-0000-0010-8000-00aa00389b71"));
            Id(type, Subtype, new(audio ? "00000001-0000-0010-8000-00aa00389b71" : "00000016-0000-0010-8000-00aa00389b71"));
            Check(((delegate* unmanaged[Stdcall]<nint, uint, nint, nint, int>)Slot(reader, 7))(reader, stream, 0, type));
            Release(ref type);
            Check(((delegate* unmanaged[Stdcall]<nint, uint, nint*, int>)Slot(reader, 6))(reader, stream, &type));
            int stride = width * 4;
            if (!audio)
            {
                Guid sizeKey = new("1652c33d-d6b2-4012-b834-72030849a37d"), strideKey = new("644b4e48-1e02-4516-b0eb-c01ca9d49ac6");
                ulong size = 0; uint strideValue = 0;
                Check(((delegate* unmanaged[Stdcall]<nint, Guid*, ulong*, int>)Slot(type, 8))(type, &sizeKey, &size));
                Program.Check((int)(size >> 32) == width && (int)(size & 0xffffffff) == height, "Decoded canvas dimensions changed.");
                if (((delegate* unmanaged[Stdcall]<nint, Guid*, uint*, int>)Slot(type, 7))(type, &strideKey, &strideValue) >= 0) stride = (int)strideValue;
            }
            int count = 0, peak = 0; long first = -1, end = -1, previous = -1, sampleCount = 0;
            byte[]? firstFrame = null, lastFrame = null;
            for (int iteration = 0; iteration < 10000; ++iteration)
            {
                uint actual = 0, flags = 0; long time = 0; nint sample = 0, buffer = 0;
                try
                {
                    Check(((delegate* unmanaged[Stdcall]<nint, uint, uint, uint*, uint*, long*, nint*, int>)Slot(reader, 9))(reader, stream, 0, &actual, &flags, &time, &sample));
                    if ((flags & 2) != 0) break;
                    if (sample == 0) continue;
                    Program.Check(time >= previous, "Decoded sample timestamps moved backwards."); previous = time;
                    if (first < 0) first = time;
                    long duration = 0;
                    Check(((delegate* unmanaged[Stdcall]<nint, long*, int>)Slot(sample, 37))(sample, &duration)); end = time + duration;
                    Check(((delegate* unmanaged[Stdcall]<nint, nint*, int>)Slot(sample, 41))(sample, &buffer));
                    byte* bytes = null; uint length = 0;
                    Check(((delegate* unmanaged[Stdcall]<nint, byte**, uint*, uint*, int>)Slot(buffer, 3))(buffer, &bytes, null, &length));
                    try
                    {
                        if (audio)
                        {
                            sampleCount += length / 2;
                            foreach (short value in new ReadOnlySpan<short>(bytes, (int)length / 2)) peak = Math.Max(peak, Math.Abs((int)value));
                        }
                        else
                        {
                            var frame = new ReadOnlySpan<byte>(bytes, (int)length).ToArray();
                            firstFrame ??= frame; lastFrame = frame;
                        }
                    }
                    finally { Check(((delegate* unmanaged[Stdcall]<nint, int>)Slot(buffer, 4))(buffer)); }
                    ++count;
                }
                finally { Release(ref buffer); Release(ref sample); }
            }
            Program.Check(count > 0, "Saved MP4 contains no decodable samples.");
            if (audio)
            {
                Program.Check(first is >= 2_000_000 and <= 3_000_000, $"Audio offset is wrong: {first}.");
                Program.Check(end is >= 16_800_000 and <= 18_800_000 && sampleCount > 65000, $"Audio gaps were lost: end={end}, samples={sampleCount}.");
                Program.Check(peak > 3000, "Recorded audio was muted or empty.");
            }
            else
            {
                int row(int y) => stride >= 0 ? y * stride : (height - 1 - y) * -stride;
                Program.Check(firstFrame![row(4) + width / 2 * 4 + 2] > 160 && firstFrame[row(height - 5) + width / 2 * 4] > 160,
                    "Video orientation or color channels changed.");
                if (width == 96)
                {
                    Program.Check(count >= 48 && end is >= 19_000_000 and <= 22_000_000, $"Video timing is wrong: frames={count}, end={end}.");
                    Program.Check(lastFrame![row(height / 2) + 4 * 4] < 30 && lastFrame[row(height / 2) + 4 * 4 + 2] < 30,
                        "Resolution change stretched the content instead of preserving its aspect ratio.");
                }
                else Program.Check(count >= 2 && end is >= 10_000_000 and <= 12_000_000, "Video-only MP4 is truncated.");
            }
            Console.WriteLine($"  decoded {(audio ? "AAC" : "H.264")}: samples={count}, first={first}, end={end}, peak={peak}");
        }
        finally { Release(ref type); Release(ref reader); Release(ref attributes); }
    }
    [DllImport("ole32.dll")] private static extern int CoInitializeEx(nint reserved, uint mode);
    [DllImport("ole32.dll")] private static extern void CoUninitialize();
    [DllImport("mfplat.dll")] private static extern int MFStartup(uint version, uint flags);
    [DllImport("mfplat.dll")] private static extern int MFShutdown();
    [DllImport("mfplat.dll")] private static extern int MFCreateAttributes(out nint attributes, uint count);
    [DllImport("mfplat.dll")] private static extern int MFCreateMediaType(out nint type);
    [DllImport("mfreadwrite.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern int MFCreateSourceReaderFromURL(string path, nint attributes, out nint reader);
}
