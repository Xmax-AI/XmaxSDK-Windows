namespace Xmax.SDK;

public sealed record TransferProgress(int State, ulong Completed, ulong Total);
