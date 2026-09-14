using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    public const uint HostAbiVersion = 1u;
    public const uint CompositeAppInvalid = 0u;
    public const uint CompositeAppA = 1u;
    public const uint CompositeAppB = 2u;
    public const uint CompositeAppCounter = 3u;
    public const uint MaxLaunchContextBytes = 48u;
    public const uint LegacyContextSize = 24u;
    public const uint HostCallTablePrefixSize = 16u;
    public const uint HostCallTableSize = 72u;
    public const uint LaunchFlagAction = 0x80000000u;
    public const uint LaunchFlagCapabilityProbe = 0x40000000u;
    public const uint LaunchFlagAbiProbe = 0x20000000u;
    public const uint LaunchFlagPayloadMask = 0x1FFFFFFFu;
    public const int ErrorInvalidArgument = -2;
    public const int ErrorUnsupported = -3;
    public const int ErrorInvalidApplicationId = -4;
}

[Flags]
public enum GuideXosCapability : ulong
{
    None = 0,
    Surface = 1ul << 0,
    Text = 1ul << 1,
    Primitive = 1ul << 2,
    Action = 1ul << 3,
    Close = 1ul << 4,
    LaunchContext = 1ul << 5,
    Log = 1ul << 6,
}

public enum GuideXosResult
{
    Success = 0,
    InvalidArgument = -2,
    CapabilityUnavailable = -3,
    InvalidApplication = -4,
    SurfaceCreationFailed = -5,
    InvalidAction = -6,
    AbiIncompatible = -7,
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
    public ulong capabilities;
    public delegate* unmanaged<NativeGxAppContext*, ulong, int, int, int, int, byte*, uint, int*, int> addActionButton;
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
