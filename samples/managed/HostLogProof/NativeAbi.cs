using System.Runtime.InteropServices;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    public const uint CompositeAppInvalid = 0u;
    public const uint CompositeAppA = 1u;
    public const uint CompositeAppB = 2u;
    public const uint MaxLaunchContextBytes = 48u;
    public const uint LegacyContextSize = 24u;
    public const int ErrorInvalidArgument = -2;
    public const int ErrorUnsupported = -3;
    public const int ErrorInvalidApplicationId = -4;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeHostCallTable
{
    public uint size;
    public uint version;
    public delegate* unmanaged<NativeGxAppContext*, byte*, int> log;
    public delegate* unmanaged<NativeGxAppContext*, byte*, int, int, ulong*, int> requestWindow;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, byte*, int> drawText;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, int, int, uint, int> drawRect;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, int, int, byte*, int*, int> addButton;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int> closeWindow;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeGxAppContext
{
    public uint size;
    public uint apiVersion;
    public NativeHostCallTable* host;
    public void* userData;
    public byte* launchContext;
    public uint launchContextLength;
    public uint launchFlags;
}
