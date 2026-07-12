using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class Program
{
    public static void Main()
    {
    }

    [UnmanagedCallersOnly(EntryPoint = "ManagedMain")]
    public static int ManagedMain(NativeGxAppContext* ctx)
    {
        if (ctx == null || ctx->host == null || ctx->host->log == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        ReadOnlySpan<byte> messageUtf8 = "Hello from managed guideXOS code"u8;
        Span<byte> messageBuffer = stackalloc byte[messageUtf8.Length + 1];
        messageUtf8.CopyTo(messageBuffer);
        messageBuffer[messageUtf8.Length] = 0;

        fixed (byte* message = messageBuffer)
        {
            return ctx->host->log(ctx, message);
        }
    }
}
