namespace Xmax.SDK;

public sealed class XmaxException(int result, string message, int apiCode = 0, int httpStatus = 0) : Exception(message)
{
    public int Result { get; } = result;
    public int ApiCode { get; } = apiCode;
    public int HttpStatus { get; } = httpStatus;
}
