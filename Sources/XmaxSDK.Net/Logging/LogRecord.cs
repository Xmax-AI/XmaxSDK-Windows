namespace Xmax.SDK;

public sealed record LogRecord(int Level, string Category, string Message, long TimestampMicroseconds, ulong Sequence);
