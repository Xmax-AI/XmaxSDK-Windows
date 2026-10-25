using System.Buffers;
using Xmax.SDK;

namespace XLab.Shared.Recording;

/// <summary>Owns callback packets, an encoder thread, and the temporary clip until save or discard.</summary>
internal sealed class RemoteMediaRecorder : IAsyncDisposable
{
    private const long Ticks = 10_000_000;
    private readonly object gate = new();
    private readonly Queue<IRecordingPacket> packets = new();
    private readonly TaskCompletionSource<RecordingResult> completion = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly Func<IRecordingWriter> createWriter;
    private readonly Action interrupted;
    private readonly string directory = Path.Combine(Path.GetTempPath(), "XLab", Guid.NewGuid().ToString("N"));
    private readonly int packetLimit;
    private readonly long byteLimit;
    private int outstanding;
    private long outstandingBytes;
    private bool accepting = true;
    private string? failure;
    private long? origin, lastVideo, lastAudio, audioEnd;
    private ulong? stream;
    private int audioRate, audioChannels;
    private bool opened;
    private readonly Queue<IRecordingPacket> prefix = new();
    private long prefixBytes;

    internal RemoteMediaRecorder(Action interrupted, Func<IRecordingWriter>? createWriter = null,
        int packetLimit = 128, long byteLimit = 64L * 1024 * 1024)
    {
        this.interrupted = interrupted;
        this.createWriter = createWriter ?? (() => new MediaFoundationWriter());
        this.packetLimit = packetLimit;
        this.byteLimit = byteLimit;
        Directory.CreateDirectory(directory);
        var worker = new Thread(Run) { IsBackground = true, Name = "XLab MP4 encoder" };
        worker.Start();
    }

    internal void Append(XmaxMediaFrame frame)
    {
        IRecordingPacket packet;
        try { packet = new RemoteRecordingPacket(frame); }
        catch (Exception error) { frame.Dispose(); Fail(error.Message); return; }
        Append(packet);
    }

    // Ownership transfers even when the queue has already stopped accepting input.
    internal void Append(IRecordingPacket packet)
    {
        bool accepted = false, overflow = false;
        try
        {
            int bytes = packet.ByteCount;
            lock (gate)
            {
                if (accepting)
                {
                    if (bytes <= 0 || outstanding >= packetLimit || bytes > byteLimit - outstandingBytes) overflow = true;
                    else
                    {
                        ++outstanding; outstandingBytes += bytes;
                        packets.Enqueue(packet); accepted = true; Monitor.PulseAll(gate);
                    }
                }
            }
        }
        catch (Exception error) { Fail(error.Message); }
        finally { if (!accepted) packet.Dispose(); }
        if (overflow) Fail("Recording stopped because the encoder could not keep up. Save the recorded portion.");
    }

    internal void StopAccepting()
    { lock (gate) { accepting = false; Monitor.PulseAll(gate); } }

    internal void Fail(string message)
    {
        bool notify;
        lock (gate)
        {
            notify = failure is null;
            failure ??= message; accepting = false; Monitor.PulseAll(gate);
        }
        if (notify) { try { interrupted(); } catch { /* A UI observer cannot take down the media callback. */ } }
    }

    internal Task<RecordingResult> FinishAsync() { StopAccepting(); return completion.Task; }

    private static long Time(MediaTime value)
    {
        if (value.Timescale <= 0 || value.Value < 0) throw new InvalidDataException("The remote media timestamp is invalid.");
        return checked((long)((Int128)value.Value * Ticks / value.Timescale));
    }

    private void Run()
    {
        string path = Path.Combine(directory, "recording.mp4");
        IRecordingWriter? writer = null;
        bool finalized = false;
        try
        {
            writer = createWriter();
            while (true)
            {
                IRecordingPacket packet;
                lock (gate)
                {
                    while (packets.Count == 0 && accepting) Monitor.Wait(gate);
                    if (packets.Count == 0) break;
                    packet = packets.Dequeue();
                }
                try { Process(packet, writer, path); }
                finally
                {
                    lock (gate) { --outstanding; outstandingBytes -= packet.ByteCount; }
                    // A pre-video audio packet moves to the bounded prefix instead.
                    if (!prefix.Contains(packet)) packet.Dispose();
                }
            }
        }
        catch (Exception error) { Fail(error.Message); }
        finally
        {
            if (opened && writer is not null)
            {
                try { writer.Finish(); finalized = true; }
                catch (Exception error) { Fail(error.Message); }
            }
            try { writer?.Dispose(); } catch (Exception error) { finalized = false; Fail(error.Message); }
            lock (gate)
            {
                while (packets.TryDequeue(out var remaining)) remaining.Dispose();
                outstanding = 0; outstandingBytes = 0;
            }
            while (prefix.TryDequeue(out var remaining)) remaining.Dispose();
            if (!finalized) { try { if (File.Exists(path)) File.Delete(path); } catch (IOException) { } }
            completion.TrySetResult(new(finalized ? path : null, failure ?? (opened ? null : "No remote video frames were recorded.")));
        }
    }

    private void Process(IRecordingPacket packet, IRecordingWriter writer, string path)
    {
        if (packet.Type is not (MediaType.Video or MediaType.Audio)) return;
        if (stream.HasValue && stream != packet.StreamId) throw new InvalidDataException("The remote stream changed. Save the recorded portion and start a new recording.");
        stream ??= packet.StreamId;
        long time = Time(packet.Time);
        if (packet.Type == MediaType.Audio && !origin.HasValue)
        {
            if (packet.ByteCount > 2 * 1024 * 1024) throw new InvalidDataException("The remote audio packet is too large.");
            while (prefix.Count >= 100 || prefixBytes + packet.ByteCount > 2 * 1024 * 1024)
            { var old = prefix.Dequeue(); prefixBytes -= old.ByteCount; old.Dispose(); }
            prefix.Enqueue(packet); prefixBytes += packet.ByteCount;
            return;
        }
        if (packet.Type == MediaType.Audio) { Audio(packet, writer, time); return; }
        if (packet.Width < 2 || packet.Height < 2 || packet.Width > 8192 || packet.Height > 8192)
            throw new InvalidDataException("The remote video dimensions are invalid.");
        if (lastVideo.HasValue && time < lastVideo) throw new InvalidDataException("The remote video clock restarted. Save the recorded portion.");
        if (time == lastVideo) return;
        if (!opened)
        {
            writer.Open(path, packet.Width & ~1, packet.Height & ~1);
            opened = true; origin = time;
        }
        long duration = packet.Duration.Timescale > 0 && packet.Duration.Value > 0 ? Time(packet.Duration) : Ticks / 30;
        packet.ReadVideo((pixels, stride, size) => writer.Video(pixels, stride, size, packet.Width, packet.Height, time - origin!.Value, duration));
        lastVideo = time;
        while (prefix.TryDequeue(out var audio))
        {
            prefixBytes -= audio.ByteCount;
            try { Audio(audio, writer, Time(audio.Time)); } finally { audio.Dispose(); }
        }
    }

    private unsafe void Audio(IRecordingPacket packet, IRecordingWriter writer, long time)
    {
        int rate = packet.SampleRate, channels = packet.Channels, samples = packet.Samples;
        if (rate is < 8000 or > 192000 || channels is < 1 or > 2 || samples <= 0 || samples > rate)
            throw new InvalidDataException("The remote audio format is invalid.");
        if (audioRate != 0 && (audioRate != rate || audioChannels != channels))
            throw new InvalidDataException("The remote audio format changed. Save the recorded portion.");
        audioRate = rate; audioChannels = channels;
        long relative = time - origin!.Value;
        if (lastAudio.HasValue && relative < lastAudio - Ticks) throw new InvalidDataException("The remote audio clock restarted. Save the recorded portion.");
        lastAudio = relative;
        long trimTicks = Math.Max(0, Math.Max(0, audioEnd ?? 0) - relative);
        int skip = (int)Int128.Min(samples, ((Int128)trimTicks * rate + Ticks - 1) / Ticks);
        if (skip == samples) return;
        long begin = Math.Max(0, relative + (long)skip * Ticks / rate);
        int count = checked((int)((long)(samples - skip) * 48000 / rate));
        if (count == 0) return;
        byte[] output = ArrayPool<byte>.Shared.Rent(count * 2);
        try
        {
            packet.ReadAudio((pointer, length) =>
            {
                if (length < checked(samples * channels * 2)) throw new InvalidDataException("The remote PCM buffer is incomplete.");
                var source = (short*)pointer;
                fixed (byte* bytes = output)
                {
                    var target = (short*)bytes;
                    for (int i = 0; i < count; ++i)
                    {
                        double position = skip + (double)i * rate / 48000;
                        int a = Math.Min((int)position, samples - 1), b = Math.Min(a + 1, samples - 1);
                        double first = source[a * channels], second = source[b * channels];
                        if (channels == 2) { first = (first + source[a * 2 + 1]) / 2; second = (second + source[b * 2 + 1]) / 2; }
                        target[i] = (short)Math.Clamp(Math.Round(first + (second - first) * (position - a), MidpointRounding.AwayFromZero), short.MinValue, short.MaxValue);
                    }
                }
            });
            writer.Audio(output.AsSpan(0, count * 2), begin);
            audioEnd = begin + (long)count * Ticks / 48000;
        }
        finally { ArrayPool<byte>.Shared.Return(output); }
    }

    /// <summary>Stages next to the destination so a failed copy never replaces an existing file.</summary>
    internal static async Task SaveAsync(string source, string destination)
    {
        string fullSource = Path.GetFullPath(source), fullDestination = Path.GetFullPath(destination);
        if (string.Equals(Path.GetDirectoryName(fullSource), Path.GetDirectoryName(fullDestination), StringComparison.OrdinalIgnoreCase))
            throw new IOException("Choose a destination outside the temporary recording folder.");
        string staging = fullDestination + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await using (var input = new FileStream(fullSource, FileMode.Open, FileAccess.Read, FileShare.Read, 81920, true))
            await using (var output = new FileStream(staging, FileMode.CreateNew, FileAccess.Write, FileShare.None, 81920, true))
            { await input.CopyToAsync(output); output.Flush(true); }
            File.Move(staging, fullDestination, true);
        }
        finally { if (File.Exists(staging)) File.Delete(staging); }
    }

    public async ValueTask DisposeAsync()
    {
        await FinishAsync();
        if (Directory.Exists(directory)) Directory.Delete(directory, true);
    }
}
