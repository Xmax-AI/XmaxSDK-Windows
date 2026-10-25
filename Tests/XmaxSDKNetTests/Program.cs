using System.Runtime.InteropServices;
using Xmax.SDK;
using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
static async Task ThrowsAsync<T>(Func<Task> body) where T : Exception
{ try { await body(); } catch (T) { return; } throw new Exception($"Expected {typeof(T).Name}"); }
static unsafe NativeHandle CreateNativeClient()
{
    var options = new xmax_client_options { struct_size = (uint)sizeof(xmax_client_options), abi_version = 1 };
    nint value = 0;
    NativeError.Check(NativeMethods.xmax_client_create_with_options(&options, &value));
    return new(value, HandleKind.Client);
}

int passed = 0;
async Task Test(string name, Func<Task> body)
{ await body(); ++passed; Console.WriteLine($"PASS {name}"); }

try
{
    if (args.Length > 0)
        await Test("C and C# layouts and event values match the public ABI", () =>
        {
            foreach (var line in File.ReadAllLines(args[0]))
            {
                string[] items = line.Split(' ');
                var type = typeof(XmaxClient).Assembly.GetType((items[0] == "enum" ? "Xmax.SDK." : "Xmax.SDK.Interop.") + items[1])!;
                long actual = items[0] == "enum" ? Convert.ToInt64(Enum.Parse(type, items[2])) : items[0] == "sizeof" ? Marshal.SizeOf(type) : Marshal.OffsetOf(type, items[2]).ToInt64();
                Check(actual == long.Parse(items[^1]), $"ABI mismatch: {line}, managed {actual}");
            }
            return Task.CompletedTask;
        });
    await Test("model capabilities, client ownership and ABI flags", async () =>
    {
        Check(XmaxRuntime.Version.Length > 0, "version");
        var model = XmaxClient.GetModelInfo();
        Check(model.SupportedFormats.Count == 2 && model.DefaultCameraFormat == new VideoFormat(1024, 1920), "model sizes");
        var client = new XmaxClient();
        Check(client.GetCapabilities().HasFlag(XmaxCapabilities.Storage), "storage capability");
        var realtime = client.CreateRealtime();
        client.Dispose();
        await using (realtime)
        {
            await realtime.PrepareExternalAsync(new(16, 8));
            Check((await realtime.GetStateAsync()).State == RealtimeState.Ready, "independent child");
        }
    });
    await Test("UTF-8 errors and async thread-local diagnostics", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        try { await realtime.PrepareExternalAsync(new(3, 5)); throw new Exception("Expected invalid argument"); }
        catch (XmaxException error) { Check(error.Result == -1 && error.Message.Length > 0, "structured error"); }
        await realtime.PrepareExternalAsync(new(16, 8));
        await ThrowsAsync<XmaxException>(() => realtime.StartAsync(new("中文提示词 👋")));
        Check((await realtime.GetStateAsync()).State == RealtimeState.Ready, "failed generation retains input");
    });
    await Test("strided BGRA ownership, mirror metadata and retained frames", async () =>
    {
        using var client = new XmaxClient();
        var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        await realtime.SetVideoMirrorAsync(new(true, false, false));
        byte[] pixels = Enumerable.Repeat((byte)127, 80 * 8).ToArray();
        sink.PushBgra(pixels, 80, 1000);
        using var frame = realtime.LocalVideo.TryAcquireFrame() ?? throw new Exception("No local frame");
        Array.Fill(pixels, (byte)0);
        Check(frame.Width == 16 && frame.Height == 8 && frame.Mirrored, "frame metadata");
        Check(frame.CopyBgra()[0] == 127, "push must copy pixels");
        await realtime.DisposeAsync();
        Check(frame.CopyBgra()[0] == 127, "frame outlives realtime");
        Check(!sink.IsRequestingFrames, "source becomes inert after destruction");
        sink.PushBgra(pixels, 80, 2000);
    });
    await Test("source replacement discards stale frames", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        using var old = await realtime.BindExternalAsync(new(16, 8));
        using var current = await realtime.BindExternalAsync(new(16, 8));
        byte[] pixels = new byte[64 * 8];
        old.PushBgra(pixels, 64, 1000);
        Check(realtime.LocalVideo.TryAcquireFrame() is null, "stale binding ignored");
        current.PushBgra(pixels, 64, 2000);
        using var frame = realtime.LocalVideo.TryAcquireFrame();
        Check(frame?.TimestampMicroseconds == 2000, "current binding accepted");
    });
    await Test("NV12 and I420 preserve plane layout through the managed boundary", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        sink.PushNv12(new byte[20 * 8], 20, Enumerable.Repeat((byte)45, 20 * 4).ToArray(), 20, 1000);
        using (var frame = realtime.LocalVideo.TryAcquireFrame()!)
        {
            Check(frame.PixelFormat == VideoPixelFormat.Nv12, "NV12 format");
            frame.ReadPlane(1, (pointer, stride, size) => Check(Marshal.ReadByte(pointer) == 45 && stride >= 16 && size >= 64, "NV12 chroma plane"));
        }
        sink.PushI420(new byte[20 * 8], 20, new byte[10 * 4], 10, Enumerable.Repeat((byte)67, 10 * 4).ToArray(), 10, 2000);
        using (var frame = realtime.LocalVideo.TryAcquireFrame()!)
        {
            Check(frame.PixelFormat == VideoPixelFormat.I420, "I420 format");
            frame.ReadPlane(2, (pointer, stride, size) => Check(Marshal.ReadByte(pointer) == 67 && stride >= 8 && size >= 32, "I420 V plane"));
        }
    });
    await Test("event callback failures are isolated", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        var delivered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        realtime.EventReceived += _ => throw new InvalidOperationException("Application callback failure");
        realtime.CallbackFaulted += _ => delivered.TrySetResult();
        await realtime.PrepareExternalAsync(new(16, 8));
        await delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Check((await realtime.GetStateAsync()).State == RealtimeState.Ready, "callback must not break lifecycle");
    });
    await Test("cancelled admission and repeated asynchronous disposal", async () =>
    {
        using var client = new XmaxClient();
        var realtime = client.CreateRealtime();
        using var stop = new CancellationTokenSource(); stop.Cancel();
        await ThrowsAsync<OperationCanceledException>(() => realtime.ConnectAsync(cancellationToken: stop.Token));
        await Task.WhenAll(realtime.DisposeAsync().AsTask(), realtime.DisposeAsync().AsTask());
        await ThrowsAsync<ObjectDisposedException>(() => realtime.PrepareExternalAsync(new(16, 8)));
    });
    await Test("cancellation can interrupt blocking native work without an admission deadlock", async () =>
    {
        using var stop = new CancellationTokenSource();
        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var task = Task.Run(() => NativeCancellation.Run(stop.Token, () =>
        { started.Set(); Check(release.Wait(5000), "cancel did not run concurrently"); return 1; }, () => release.Set()));
        Check(started.Wait(5000), "operation did not begin");
        await stop.CancelAsync();
        await ThrowsAsync<OperationCanceledException>(async () => await task);
    });
    await Test("cancellation also reaches an operation admitted after its first cancel request", async () =>
    {
        using var stop = new CancellationTokenSource();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        int ready = 0;
        var task = Task.Run(() => NativeCancellation.Run(stop.Token, () =>
        {
            entered.Set(); Thread.Sleep(60); Volatile.Write(ref ready, 1);
            Check(release.Wait(3000), "early cancellation missed late native admission");
            return 1;
        }, () => { if (Volatile.Read(ref ready) == 1) release.Set(); }));
        Check(entered.Wait(3000), "operation did not enter");
        await stop.CancelAsync();
        await ThrowsAsync<OperationCanceledException>(async () => await task);
    });
    await Test("storage cancellation, error propagation and disposal", async () =>
    {
        using var client = new XmaxClient();
        await using var storage = client.CreateStorage();
        Check(storage.GetProgress().State == 0, "initial progress");
        await ThrowsAsync<XmaxException>(() => storage.UploadFileAsync("missing-中文.png"));
        storage.Cancel();
    });
    await Test("disposal cancels late admission and retains the native handle until completion", async () =>
    {
        using var handle = CreateNativeClient();
        var operations = new OperationTracker(handle);
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        int ready = 0;
        var task = operations.Run(value =>
        {
            entered.Set(); Thread.Sleep(60); Volatile.Write(ref ready, 1);
            Check(release.Wait(3000), "close missed late admission");
            Check(!handle.IsClosed, "native handle released while in use");
            return 0;
        });
        Check(entered.Wait(3000), "operation did not start");
        await operations.DisposeAsync(_ => { if (Volatile.Read(ref ready) == 1) release.Set(); }).WaitAsync(TimeSpan.FromSeconds(5));
        await task;
        Check(handle.IsClosed, "native handle was not released");
    });
    await Test("remote callback registration survives GC and supports replacement", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        await realtime.SetRemoteMediaListenerAsync(packet => packet.Dispose());
        GC.Collect(); GC.WaitForPendingFinalizers();
        await realtime.SetRemoteMediaListenerAsync(packet => packet.Dispose());
        await realtime.SetRemoteMediaListenerAsync(null);
        Check(!realtime.GetVideoStatistics(0).Available, "statistics must be unavailable before connection");
    });
    Console.WriteLine($"{passed} managed SDK tests passed");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }
