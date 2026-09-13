using System.Runtime.InteropServices;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    public const uint CompositeAppInvalid = 0u;
    public const uint CompositeAppA = 1u;
    public const uint CompositeAppB = 2u;
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
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeGxAppContext
{
    public uint size;
    public uint apiVersion;
    public NativeHostCallTable* host;
    public void* userData;
}
