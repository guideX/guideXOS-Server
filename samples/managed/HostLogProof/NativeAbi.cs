using System;
using System.Runtime.InteropServices;
using System.Text;

namespace HostLogProof;

public static class GxAbi
{
    public const uint ApiVersion = 0u;
    // ABI v2 appends the read-only application-snapshot callback after the
    // complete 104-byte v1 table. ABI-v1 consumers still use their prefix.
    public const uint HostAbiVersion = 2u;
    public const uint HostAbiV1Version = 1u;
    public const uint CompositeAppInvalid = 0u;
    public const uint CompositeAppA = 1u;
    public const uint CompositeAppB = 2u;
    public const uint CompositeAppCounter = 3u;
    public const uint MaxLaunchContextBytes = 48u;
    public const uint LegacyContextSize = 24u;
    public const uint HostCallTablePrefixSize = 16u;
    public const uint HostCallTableV1Size = 72u;
    public const uint C113HostCallTableSize = 88u;
    public const uint HostCallTableV1FullSize = 104u;
    public const uint HostCallTableSize = 112u;
    public const uint ApplicationSnapshotOffset = 104u;
    public const uint ApplicationSnapshotRecordVersion = 1u;
    public const uint ApplicationSnapshotRecordSize = 168u;
    public const uint ApplicationSnapshotCapacity = 18u;
    public const uint ApplicationSnapshotDisplayNameBytes = 32u;
    public const uint ApplicationSnapshotApplicationIdBytes = 96u;
    public const uint ApplicationSnapshotBufferBytes =
        ApplicationSnapshotRecordSize * ApplicationSnapshotCapacity;
    public const uint DirectoryListOffset = 88u;
    public const uint FileStatOffset = 96u;
    public const ulong CapabilityApplicationSnapshot = 1ul << 11;
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
    // Modifier bits are key-event-only. Bit 23 preserves C117 Shift and bit
    // 22 carries C156 Control; the low 22 bits remain the key value. Pointer
    // coordinate payloads retain their full existing 12+12-bit layout.
    public const uint LaunchFlagInputShift = 0x00800000u;
    public const uint LaunchFlagInputControl = 0x00400000u;
    public const uint LaunchFlagInputValueMask = 0x003FFFFFu;
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
    ApplicationSnapshot = 1ul << 11,
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
    public delegate* unmanaged<NativeGxAppContext*, NativeApplicationSnapshotRecord*, uint, uint*, uint*, int> applicationSnapshot;
}

[StructLayout(LayoutKind.Sequential, Pack = 8, Size = (int)GxAbi.ApplicationSnapshotRecordSize)]
public unsafe struct NativeApplicationSnapshotRecord
{
    public uint recordVersion;
    public uint source;
    public ulong instanceId;
    public uint state;
    public uint flags;
    public uint displayNameLength;
    public uint applicationIdLength;
    public uint reserved0;
    public uint reserved1;
    public fixed byte displayName[(int)GxAbi.ApplicationSnapshotDisplayNameBytes];
    public fixed byte applicationId[(int)GxAbi.ApplicationSnapshotApplicationIdBytes];
}

public enum GuideXosApplicationSnapshotSource : uint
{
    AppManagerInstance = 1,
    ShellSurface = 2,
    ManagedLogicalApplication = 3,
}

public enum GuideXosApplicationSnapshotState : uint
{
    NotLoaded = 0,
    Running = 1,
    Suspended = 2,
    Terminated = 3,
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct GuideXosApplicationSnapshotRecord
{
    public uint recordVersion;
    public GuideXosApplicationSnapshotSource source;
    public ulong instanceId;
    public GuideXosApplicationSnapshotState state;
    public uint flags;
    public uint displayNameLength;
    public uint applicationIdLength;
    public uint reserved0;
    public uint reserved1;
    public fixed byte displayName[(int)GxAbi.ApplicationSnapshotDisplayNameBytes];
    public fixed byte applicationId[(int)GxAbi.ApplicationSnapshotApplicationIdBytes];

    public readonly GuideXosApplicationInstanceId Identity =>
        new(source, instanceId);
    public readonly bool IsActive => (flags & 1u) != 0u;

    public readonly string GetDisplayName()
    {
        if (displayNameLength >= GxAbi.ApplicationSnapshotDisplayNameBytes)
            return string.Empty;
        fixed (byte* value = displayName)
        {
            return Encoding.ASCII.GetString(new ReadOnlySpan<byte>(value,
                (int)displayNameLength));
        }
    }

    public readonly string GetApplicationId()
    {
        if (applicationIdLength >= GxAbi.ApplicationSnapshotApplicationIdBytes)
            return string.Empty;
        fixed (byte* value = applicationId)
        {
            return Encoding.ASCII.GetString(new ReadOnlySpan<byte>(value,
                (int)applicationIdLength));
        }
    }
}

public readonly struct GuideXosApplicationInstanceId : IEquatable<GuideXosApplicationInstanceId>
{
    public GuideXosApplicationInstanceId(
        GuideXosApplicationSnapshotSource source, ulong value)
    {
        Source = source;
        Value = value;
    }

    public GuideXosApplicationSnapshotSource Source { get; }
    public ulong Value { get; }
    public bool Equals(GuideXosApplicationInstanceId other) =>
        Source == other.Source && Value == other.Value;
    public override bool Equals(object obj) =>
        obj is GuideXosApplicationInstanceId other && Equals(other);
    public override int GetHashCode() => unchecked(
        (int)(Value ^ (Value >> 32)) ^ (int)Source);
    public static bool operator ==(GuideXosApplicationInstanceId left,
        GuideXosApplicationInstanceId right) => left.Equals(right);
    public static bool operator !=(GuideXosApplicationInstanceId left,
        GuideXosApplicationInstanceId right) => !left.Equals(right);
}

public enum GuideXosApplicationSnapshotResult
{
    Success = 0,
    NotSupported = 1,
    CapabilityUnavailable = 2,
    InvalidData = 3,
    Truncated = 4,
    NativeFailure = 5,
    InvalidArgument = 6,
}

[StructLayout(LayoutKind.Sequential, Pack = 8)]
public unsafe struct GuideXosApplicationSnapshot
{
    internal uint totalCount;
    internal uint copiedCount;
    internal int nativeResult;
    internal uint reserved;
    // Fixed ulong storage forces an 8-byte-aligned record array while keeping
    // the exact 3024-byte native record capacity.
    internal fixed ulong recordStorage[(int)(GxAbi.ApplicationSnapshotBufferBytes / sizeof(ulong))];

    public readonly uint TotalCount => totalCount;
    public readonly uint Count => copiedCount;
    public readonly bool IsTruncated => nativeResult == 1;

    public readonly bool TryGetRecord(uint index,
        out GuideXosApplicationSnapshotRecord record)
    {
        record = default;
        if (index >= copiedCount) return false;
        fixed (ulong* storage = recordStorage)
        {
            record = *(GuideXosApplicationSnapshotRecord*)((byte*)storage +
                (int)(index * GxAbi.ApplicationSnapshotRecordSize));
        }
        return true;
    }
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
