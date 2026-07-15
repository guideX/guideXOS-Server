using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class Program
{
    [DllImport("__Internal", EntryPoint = "guideXosManagedArrayHostLog")]
    private static extern int GuideXosManagedArrayHostLog(NativeGxAppContext* context, nint arrayObject);

    public static void Main()
    {
    }

    [UnmanagedCallersOnly(EntryPoint = "ManagedMain")]
    public static int ManagedMain(NativeGxAppContext* ctx)
    {
        if (ctx == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

        if (ctx->size < (uint)sizeof(NativeGxAppContext))
        {
            return GxAbi.ErrorInvalidArgument;
        }

        if (ctx->apiVersion != GxAbi.ApiVersion)
        {
            return GxAbi.ErrorUnsupported;
        }

        if (ctx->host == null || ctx->host->log == null)
        {
            return GxAbi.ErrorInvalidArgument;
        }

#if HOSTLOGPROOF_ALLOCATING
        // The existing guideXOS host ABI accepts a NUL-terminated byte pointer,
        // so the managed array contains 23 UTF-8 message bytes and one managed
        // terminator. The host callback is synchronous and does not retain it.
        byte[] messageBuffer = new byte[]
        {
            (byte)'H', (byte)'e', (byte)'l', (byte)'l', (byte)'o', (byte)' ',
            (byte)'f', (byte)'r', (byte)'o', (byte)'m', (byte)' ',
            (byte)'m', (byte)'a', (byte)'n', (byte)'a', (byte)'g', (byte)'e',
            (byte)'d', (byte)' ', (byte)'h', (byte)'e', (byte)'a', (byte)'p',
            0
        };

        // The guideXOS runtime-pack helper keeps object-layout knowledge inside
        // the runtime layer and calls the existing synchronous host ABI with
        // array data. The raw object reference is valid because this experiment
        // disables collection and the local reference remains live through the
        // helper call.
        nint arrayObject = System.Runtime.CompilerServices.Unsafe.As<byte[], nint>(ref messageBuffer);
        int result = GuideXosManagedArrayHostLog(ctx, arrayObject);
        GC.KeepAlive(messageBuffer);
        return result;
#else
        ReadOnlySpan<byte> messageUtf8 = "Hello from managed guideXOS code"u8;
        Span<byte> messageBuffer = stackalloc byte[messageUtf8.Length + 1];
        messageUtf8.CopyTo(messageBuffer);
        messageBuffer[messageUtf8.Length] = 0;

        fixed (byte* message = messageBuffer)
        {
            return ctx->host->log(ctx, message);
        }
#endif
    }
}
