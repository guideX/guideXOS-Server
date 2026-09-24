using System;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    // C113 is an append-only extension of the C112 v1 table.  Keeping the
    // version stable lets older C112 clients consume the original prefix.
    public const uint HostAbiVersion = 1u;
    public const uint CompositeAppInvalid = 0u;
    public const uint CompositeAppA = 1u;
    public const uint CompositeAppB = 2u;
    public const uint CompositeAppCounter = 3u;
    public const uint MaxLaunchContextBytes = 48u;
    public const uint LegacyContextSize = 24u;
    public const uint HostCallTablePrefixSize = 16u;
    public const uint HostCallTableV1Size = 72u;
    public const uint C113HostCallTableSize = 88u;
    public const uint HostCallTableSize = 104u;
    public const uint DirectoryListOffset = 88u;
    public const uint FileStatOffset = 96u;
    public const uint FilePathMaxBytes = 96u;
    public const uint MaxFileBytes = 16u * 1024u;
    public const uint MaxDirectoryEntries = 64u;
    public const uint MaxDirectoryNameBytes = 127u;
    public const uint DirectoryEntryAbiSize = 144u;
    public const uint FileInfoAbiSize = 16u;
    public const uint LaunchFlagAction = 0x80000000u;
    public const uint LaunchFlagCapabilityProbe = 0x40000000u;
    public const uint LaunchFlagAbiProbe = 0x20000000u;
    // C116 reuses the existing launch-flags transport for synchronous input
    // re-entry.  The host table and its ABI version remain unchanged.
    public const uint LaunchFlagInput = 0x10000000u;
    public const uint LaunchFlagInputKindMask = 0x0F000000u;
    public const uint LaunchFlagInputPointerDown = 0x01000000u;
    public const uint LaunchFlagInputKeyDown = 0x02000000u;
    public const uint LaunchFlagInputKeyChar = 0x03000000u;
    public const uint LaunchFlagInputPointerUp = 0x04000000u;
    public const uint LaunchFlagInputSecondaryPointerDown = 0x05000000u;
    public const uint LaunchFlagInputSecondaryPointerUp = 0x06000000u;
    // C137 keeps X/Y in the existing 24-bit payload. Wheel values -4..+4 are
    // encoded by the nine remaining four-bit input-kind values.
    public const uint LaunchFlagInputWheel = 0x07000000u;
    public const uint LaunchFlagInputWheelLast = 0x0F000000u;
    public const uint LaunchFlagInputPayloadMask = 0x00FFFFFFu;
    // The high payload bit is reserved for key-event modifier state. Pointer
    // coordinates remain unchanged and are far below this bit in the current
    // managed surface geometry.
    public const uint LaunchFlagInputShift = 0x00800000u;
    public const uint LaunchFlagInputValueMask = 0x007FFFFFu;
    public const uint LaunchFlagInputCoordinateMask = 0x00000FFFu;
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
    FileRead = 1ul << 7,
    FileWrite = 1ul << 8,
    DirectoryList = 1ul << 9,
    FileStat = 1ul << 10,
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

public enum GuideXosFileResult
{
    Success = 0,
    NotFound = -10,
    InvalidPath = -11,
    BufferTooSmall = -12,
    FileTooLarge = -13,
    IoFailure = -14,
    CapabilityUnavailable = -15,
    InvalidArgument = -16,
    NotDirectory = -17,
    EntryNameTooLong = -18,
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
    public delegate* unmanaged<NativeGxAppContext*, byte*, uint, byte*, uint, uint*, int> fileReadAll;
    public delegate* unmanaged<NativeGxAppContext*, byte*, uint, byte*, uint, int> fileWriteAll;
    public delegate* unmanaged<NativeGxAppContext*, byte*, uint, byte*, uint, uint, uint*, uint*, int> directoryList;
    public delegate* unmanaged<NativeGxAppContext*, byte*, uint, byte*, uint, int> fileStat;
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public unsafe struct NativeDirectoryEntry
{
    public uint nameLength;
    public uint type;
    public ulong size;
    public fixed byte name[128];
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct NativeFileInfo
{
    public uint type;
    public uint reserved;
    public ulong size;
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
