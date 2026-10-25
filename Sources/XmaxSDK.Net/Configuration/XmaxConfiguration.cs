namespace Xmax.SDK;

public sealed record XmaxConfiguration
{
    public string ApiKey { get; init; } = "";
    public XmaxEnvironment Environment { get; init; } = XmaxEnvironment.China;
    public uint HttpTimeoutMilliseconds { get; init; } = 15000;
    public uint LoggerOptions { get; init; }
    // Avoid accidentally formatting credentials in record-generated diagnostics.
    public override string ToString() => $"XmaxConfiguration {{ Environment = {Environment} }}";
}
