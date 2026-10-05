using System;
using System.Text;

namespace HostLogProof;

/// <summary>
/// Small versioned host-service facade. Applications never call a native
/// callback or retain a native context pointer directly.
/// </summary>
public sealed unsafe class GuideXosHost
{
    // NativeHostCallTable v1: two uint32 fields followed by six amd64
    // function pointers. The managed artifact is win-x64 only.
    private const nuint CapabilitiesOffset = 56u;
    private const nuint FileReadOffset = 72u;
    private const nuint FileWriteOffset = 80u;
    private const nuint DirectoryListOffset = 88u;
    private const nuint FileStatOffset = 96u;
    private const nuint ApplicationSnapshotOffset = 104u;
    private const uint FileActivationContextSize = 64u;
    private NativeGxAppContext* _context;
    private NativeHostCallTable* _host;
    private static readonly GuideXosLaunchContext s_dispatchContext =
        new();
    private static readonly GuideXosHost s_dispatchHost =
        new();

    private GuideXosHost()
    {
    }

    public GuideXosLaunchContext LaunchContext { get; private set; }
    public uint Selector => LaunchContext.Selector;
    public GuideXosCapability Capabilities =>
        HasHostField(CapabilitiesOffset)
            ? (GuideXosCapability)_host->capabilities
            : GuideXosCapability.None;
    public bool IsAction => LaunchContext.IsAction;
    public bool IsCapabilityProbe =>
        (LaunchContext.Flags & GxAbi.LaunchFlagCapabilityProbe) != 0;
    public bool IsAbiProbe =>
        (LaunchContext.Flags & GxAbi.LaunchFlagAbiProbe) != 0;

    public bool HasCapability(GuideXosCapability capability)
    {
        return (Capabilities & capability) == capability;
    }

    internal static bool TryCreate(
        NativeGxAppContext* context,
        uint selector,
        out GuideXosHost host,
        out GuideXosResult result)
    {
        host = null;
        result = GuideXosResult.InvalidArgument;
        if (context == null || context->size < GxAbi.LegacyContextSize ||
            context->apiVersion != GxAbi.ApiVersion || context->host == null ||
            context->host->size < GxAbi.HostCallTablePrefixSize)
        {
            return false;
        }

        if (context->host->version < GxAbi.HostAbiV1Version ||
            context->host->version > GxAbi.HostAbiVersion ||
            context->host->size < GxAbi.HostCallTableV1Size)
        {
            result = GuideXosResult.AbiIncompatible;
            return false;
        }

        if (!s_dispatchContext.TryCopy(context, selector,
                out GuideXosLaunchContext launchContext) ||
            launchContext == null)
        {
            return false;
        }

        s_dispatchHost._context = context;
        s_dispatchHost._host = context->host;
        s_dispatchHost.LaunchContext = launchContext;
        host = s_dispatchHost;
        result = GuideXosResult.Success;
        return true;
    }

    public GuideXosResult TryCreateSurface(
        ReadOnlySpan<byte> title,
        int width,
        int height,
        out GuideXosSurface surface)
    {
        surface = null;
        if (!HasCapability(GuideXosCapability.Surface) ||
            _host->requestWindow == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (title.Length == 0 || title.Length > 127 || width <= 0 || height <= 0)
        {
            return GuideXosResult.InvalidArgument;
        }

        Span<byte> buffer = stackalloc byte[title.Length + 1];
        title.CopyTo(buffer);
        buffer[title.Length] = 0;
        ulong window = 0u;
        fixed (byte* pointer = buffer)
        {
            if (_host->requestWindow(_context, pointer, width, height, &window) != 0 ||
                window == 0u)
            {
                return GuideXosResult.SurfaceCreationFailed;
            }
        }

        surface = new GuideXosSurface(this, window);
        return GuideXosResult.Success;
    }

    /// <summary>Copies the current bounded application inventory from the host.</summary>
    public GuideXosApplicationSnapshotResult TryGetApplicationSnapshot(
        out GuideXosApplicationSnapshot snapshot)
    {
        return TryGetApplicationSnapshot(_context, _host, out snapshot);
    }

    /// <summary>Copies a bounded prefix of the current application inventory.</summary>
    public GuideXosApplicationSnapshotResult TryGetApplicationSnapshot(
        uint capacity, out GuideXosApplicationSnapshot snapshot)
    {
        return TryGetApplicationSnapshot(_context, _host, capacity, out snapshot);
    }

    public GuideXosApplicationCloseResult TryCloseApplication(
        GuideXosApplicationInstanceId identity)
    {
        return GuideXosApplicationControl.TryCloseApplication(
            _context, _host, identity);
    }

    /// <summary>Requests bounded OS file activation for the current Explorer dispatch.</summary>
    public GuideXosFileActivationResult TryRequestFileActivation(
        ReadOnlySpan<byte> canonicalPath)
    {
        if (LaunchContext.Selector != 8u || canonicalPath.Length == 0)
            return GuideXosFileActivationResult.InvalidPath;
        if (canonicalPath.Length > GxAbi.MaxLaunchContextBytes)
            return GuideXosFileActivationResult.PathTooLong;
        if (_context == null || _context->size < FileActivationContextSize ||
            _context->applicationRequestState == null ||
            _context->requestFileActivation == null)
            return GuideXosFileActivationResult.NotSupported;
        for (int index = 0; index < canonicalPath.Length; index++)
        {
            if (canonicalPath[index] == 0)
                return GuideXosFileActivationResult.InvalidPath;
        }

        Span<byte> path = stackalloc byte[canonicalPath.Length + 1];
        canonicalPath.CopyTo(path);
        path[^1] = 0;
        int nativeResult;
        fixed (byte* pointer = path)
        {
            nativeResult = _context->requestFileActivation(
                _context, pointer, (uint)canonicalPath.Length);
        }
        return nativeResult switch
        {
            0 => GuideXosFileActivationResult.Accepted,
            1 => GuideXosFileActivationResult.Unsupported,
            -1 => GuideXosFileActivationResult.InvalidPath,
            -2 => GuideXosFileActivationResult.PathTooLong,
            -3 => GuideXosFileActivationResult.Directory,
            -4 => GuideXosFileActivationResult.NotRegularFile,
            -5 => GuideXosFileActivationResult.NotFound,
            -6 => GuideXosFileActivationResult.IoFailure,
            _ => GuideXosFileActivationResult.NotSupported,
        };
    }

    internal static GuideXosApplicationSnapshotResult TryGetApplicationSnapshot(
        NativeGxAppContext* context,
        NativeHostCallTable* table,
        out GuideXosApplicationSnapshot snapshot)
    {
        return TryGetApplicationSnapshot(context, table,
            GxAbi.ApplicationSnapshotCapacity, out snapshot);
    }

    internal static GuideXosApplicationSnapshotResult TryGetApplicationSnapshot(
        NativeGxAppContext* context,
        NativeHostCallTable* table,
        uint capacity,
        out GuideXosApplicationSnapshot snapshot)
    {
        snapshot = default;
        if (context == null || table == null)
            return GuideXosApplicationSnapshotResult.InvalidArgument;
        if (capacity > GxAbi.ApplicationSnapshotCapacity)
            return GuideXosApplicationSnapshotResult.InvalidArgument;
        if (table->version < 2u ||
            table->size < GxAbi.ApplicationSnapshotOffset + (uint)sizeof(ulong))
            return GuideXosApplicationSnapshotResult.NotSupported;
        if ((table->capabilities & GxAbi.CapabilityApplicationSnapshot) == 0u)
            return GuideXosApplicationSnapshotResult.CapabilityUnavailable;
        if (table->applicationSnapshot == null)
            return GuideXosApplicationSnapshotResult.NotSupported;

        uint totalCount = 0u;
        uint copiedCount = 0u;
        int nativeResult;
        fixed (ulong* storage = snapshot.recordStorage)
        {
            nativeResult = table->applicationSnapshot(context,
                (NativeApplicationSnapshotRecord*)storage,
                capacity, &totalCount, &copiedCount);
        }
        if (nativeResult == -2)
            return GuideXosApplicationSnapshotResult.InvalidArgument;
        if (nativeResult == -3)
            return GuideXosApplicationSnapshotResult.CapabilityUnavailable;
        if (nativeResult == -5)
            return GuideXosApplicationSnapshotResult.NotSupported;
        if (nativeResult != 0 && nativeResult != 1)
            return GuideXosApplicationSnapshotResult.NativeFailure;
        if (!IsValidApplicationSnapshotCountTuple(nativeResult,
                totalCount, copiedCount))
            return GuideXosApplicationSnapshotResult.InvalidData;

        snapshot.totalCount = totalCount;
        snapshot.copiedCount = copiedCount;
        snapshot.nativeResult = nativeResult;
        if (!ValidateSnapshotRecords(ref snapshot))
        {
            snapshot = default;
            return GuideXosApplicationSnapshotResult.InvalidData;
        }
        return nativeResult == 1
            ? GuideXosApplicationSnapshotResult.Truncated
            : GuideXosApplicationSnapshotResult.Success;
    }

    internal static bool IsValidApplicationSnapshotCountTuple(
        int nativeResult, uint totalCount, uint copiedCount)
    {
        if (totalCount > GxAbi.ApplicationSnapshotCapacity ||
            copiedCount > GxAbi.ApplicationSnapshotCapacity ||
            copiedCount > totalCount) return false;
        return nativeResult switch
        {
            0 => copiedCount == totalCount,
            1 => copiedCount < totalCount,
            _ => false,
        };
    }

    private static bool ValidateSnapshotRecords(
        ref GuideXosApplicationSnapshot snapshot)
    {
        uint activeCount = 0u;
        for (uint index = 0u; index < snapshot.copiedCount; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record) ||
            !IsValidApplicationSnapshotRecord(ref record)) return false;
            if (record.IsActive) ++activeCount;
            for (uint priorIndex = 0u; priorIndex < index; ++priorIndex)
            {
                if (!snapshot.TryGetRecord(priorIndex,
                        out GuideXosApplicationSnapshotRecord prior) ||
                    prior.Identity == record.Identity) return false;
            }
        }
        return activeCount <= 1u && snapshot.reserved == 0u;
    }

    internal static bool IsValidApplicationSnapshotRecord(
        ref GuideXosApplicationSnapshotRecord record)
    {
        if (record.recordVersion != GxAbi.ApplicationSnapshotRecordVersion ||
            record.instanceId == 0u ||
            (uint)record.source < (uint)GuideXosApplicationSnapshotSource.AppManagerInstance ||
            (uint)record.source > (uint)GuideXosApplicationSnapshotSource.ManagedLogicalApplication ||
            (uint)record.state > (uint)GuideXosApplicationSnapshotState.Terminated ||
            (record.flags & ~1u) != 0u || record.reserved0 != 0u ||
            record.reserved1 != 0u ||
            record.displayNameLength >= GxAbi.ApplicationSnapshotDisplayNameBytes ||
            record.applicationIdLength >= GxAbi.ApplicationSnapshotApplicationIdBytes)
            return false;

        bool nameTerminated = false;
        bool appIdTerminated = record.applicationIdLength == 0u;
        fixed (byte* name = record.displayName)
        fixed (byte* appId = record.applicationId)
        {
            nameTerminated = name[record.displayNameLength] == 0u;
            if (record.applicationIdLength != 0u)
                appIdTerminated = appId[record.applicationIdLength] == 0u;
            for (uint index = 0u; index < record.displayNameLength; ++index)
            {
                if (name[index] < 0x20u || name[index] > 0x7Eu) return false;
            }
            for (uint index = 0u; index < record.applicationIdLength; ++index)
            {
                if (appId[index] < 0x20u || appId[index] > 0x7Eu) return false;
            }
        }
        if (!nameTerminated || !appIdTerminated || record.displayNameLength == 0u)
            return false;
        if (record.source == GuideXosApplicationSnapshotSource.ManagedLogicalApplication &&
            record.applicationIdLength == 0u) return false;
        if (record.source == GuideXosApplicationSnapshotSource.ShellSurface &&
            record.applicationIdLength != 0u) return false;
        return true;
    }

    public GuideXosResult TryGetSurface(ulong window, out GuideXosSurface surface)
    {
        surface = null;
        if (!HasCapability(GuideXosCapability.Surface) || window == 0u)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        surface = new GuideXosSurface(this, window);
        return GuideXosResult.Success;
    }

    public GuideXosResult TryLog(ReadOnlySpan<byte> text)
    {
        if (!HasCapability(GuideXosCapability.Log) || _host->log == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (text.Length == 0 || text.Length > 127) return GuideXosResult.InvalidArgument;

        Span<byte> buffer = stackalloc byte[text.Length + 1];
        text.CopyTo(buffer);
        buffer[text.Length] = 0;
        fixed (byte* pointer = buffer)
        {
            return _host->log(_context, pointer) == 0
                ? GuideXosResult.Success
                : GuideXosResult.InvalidArgument;
        }
    }

    /// <summary>
    /// Reads one bounded application-data file.  The returned array is
    /// managed-owned; the native callback never retains the caller buffer.
    /// </summary>
    public GuideXosFileResult TryReadAllBytes(
        ReadOnlySpan<byte> path,
        out byte[] data)
    {
        data = null;
        if (!HasCapability(GuideXosCapability.FileRead) ||
            !HasHostField(FileReadOffset) || _host->fileReadAll == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }

        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        uint length = 0u;
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        {
            nativeResult = _host->fileReadAll(_context, pathPointer,
                (uint)path.Length, null, 0u, &length);
        }
        if (nativeResult == (int)GuideXosFileResult.Success)
        {
            data = Array.Empty<byte>();
            return GuideXosFileResult.Success;
        }
        if (nativeResult != (int)GuideXosFileResult.BufferTooSmall ||
            length > GxAbi.MaxFileBytes)
        {
            return (GuideXosFileResult)nativeResult;
        }

        byte[] buffer = new byte[(int)length];
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* bufferPointer = buffer)
        {
            nativeResult = _host->fileReadAll(
                _context, pathPointer, (uint)path.Length, bufferPointer,
                (uint)buffer.Length, &length);
        }
        if (nativeResult != 0) return (GuideXosFileResult)nativeResult;
        if (length > GxAbi.MaxFileBytes) return GuideXosFileResult.IoFailure;

        data = new byte[(int)length];
        for (uint index = 0u; index < length; index++)
        {
            data[(int)index] = buffer[(int)index];
        }
        return GuideXosFileResult.Success;
    }

    /// <summary>
    /// Reads a file into caller-owned bounded storage. A larger file returns
    /// BufferTooSmall without allocating a managed buffer sized from metadata.
    /// </summary>
    public GuideXosFileResult TryReadInto(
        ReadOnlySpan<byte> path,
        Span<byte> destination,
        out int bytesRead)
    {
        bytesRead = 0;
        if (!HasCapability(GuideXosCapability.FileRead) ||
            !HasHostField(FileReadOffset) || _host->fileReadAll == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }
        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        uint length = 0u;
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* destinationPointer = destination)
        {
            nativeResult = _host->fileReadAll(_context, pathPointer,
                (uint)path.Length, destinationPointer,
                (uint)destination.Length, &length);
        }
        if (length > GxAbi.MaxFileBytes)
        {
            return GuideXosFileResult.IoFailure;
        }
        if (nativeResult != (int)GuideXosFileResult.Success)
        {
            if (nativeResult == (int)GuideXosFileResult.BufferTooSmall)
                bytesRead = (int)length;
            return (GuideXosFileResult)nativeResult;
        }
        if (length > (uint)destination.Length)
        {
            return GuideXosFileResult.IoFailure;
        }
        bytesRead = (int)length;
        return GuideXosFileResult.Success;
    }

    /// <summary>
    /// Writes one bounded application-data file. Native code copies the data
    /// into the VFS before this call returns and retains no managed pointer.
    /// </summary>
    public GuideXosFileResult TryWriteAllBytes(
        ReadOnlySpan<byte> path,
        ReadOnlySpan<byte> data)
    {
        if (!HasCapability(GuideXosCapability.FileWrite) ||
            !HasHostField(FileWriteOffset) || _host->fileWriteAll == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }
        if (data.Length > GxAbi.MaxFileBytes)
        {
            return GuideXosFileResult.FileTooLarge;
        }

        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        byte[] dataBuffer = data.ToArray();
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* dataPointer = dataBuffer)
        {
            nativeResult = _host->fileWriteAll(
                _context, pathPointer, (uint)path.Length, dataPointer,
                (uint)data.Length);
        }
        return (GuideXosFileResult)nativeResult;
    }

    /// <summary>Returns one bounded, managed-owned directory snapshot.</summary>
    public GuideXosFileResult TryListDirectory(
        ReadOnlySpan<byte> path,
        out GuideXosDirectorySnapshot snapshot)
    {
        snapshot = null;
        if (!HasCapability(GuideXosCapability.DirectoryList) ||
            !HasHostField(DirectoryListOffset) || _host->directoryList == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }

        const int capacity = (int)GxAbi.MaxDirectoryEntries;
        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        byte[] entryBuffer = new byte[capacity * (int)GxAbi.DirectoryEntryAbiSize];
        uint count = 0u;
        uint hasMore = 0u;
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* entryPointer = entryBuffer)
        {
            nativeResult = _host->directoryList(
                _context, pathPointer, (uint)path.Length, entryPointer,
                (uint)capacity, GxAbi.DirectoryEntryAbiSize, &count, &hasMore);
        }
        if (nativeResult != 0) return (GuideXosFileResult)nativeResult;
        if (count > GxAbi.MaxDirectoryEntries || hasMore > 1u)
        {
            return GuideXosFileResult.IoFailure;
        }

        GuideXosDirectoryEntry[] entries = new GuideXosDirectoryEntry[(int)count];
        fixed (byte* entryPointer = entryBuffer)
        {
            for (uint index = 0u; index < count; ++index)
            {
                NativeDirectoryEntry* nativeEntry = (NativeDirectoryEntry*)(
                    entryPointer + index * GxAbi.DirectoryEntryAbiSize);
                if (nativeEntry->nameLength > GxAbi.MaxDirectoryNameBytes)
                {
                    return GuideXosFileResult.EntryNameTooLong;
                }
                byte[] nameBytes = new byte[(int)nativeEntry->nameLength];
                for (uint byteIndex = 0u; byteIndex < nativeEntry->nameLength; ++byteIndex)
                {
                    nameBytes[(int)byteIndex] = nativeEntry->name[byteIndex];
                }
                entries[(int)index] = new GuideXosDirectoryEntry(
                    Encoding.UTF8.GetString(nameBytes),
                    nativeEntry->type == 2u ? GuideXosEntryType.Directory : GuideXosEntryType.Regular,
                    nativeEntry->size);
            }
        }
        // Keep application behavior independent of FAT on-disk order while
        // remaining bounded and deterministic.
        for (int outer = 1; outer < entries.Length; outer++)
        {
            GuideXosDirectoryEntry value = entries[outer];
            int inner = outer - 1;
            while (inner >= 0 && string.CompareOrdinal(entries[inner].Name, value.Name) > 0)
            {
                entries[inner + 1] = entries[inner];
                inner--;
            }
            entries[inner + 1] = value;
        }
        snapshot = new GuideXosDirectorySnapshot(entries, hasMore != 0u);
        return GuideXosFileResult.Success;
    }

    /// <summary>Reads stable type and regular-file length metadata.</summary>
    public GuideXosFileResult TryGetInfo(
        ReadOnlySpan<byte> path,
        out GuideXosFileInfo info)
    {
        info = null;
        if (!HasCapability(GuideXosCapability.FileStat) ||
            !HasHostField(FileStatOffset) || _host->fileStat == null)
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        if (path.Length == 0 || path.Length > GxAbi.FilePathMaxBytes)
        {
            return GuideXosFileResult.InvalidPath;
        }

        byte[] pathBuffer = new byte[path.Length + 1];
        path.CopyTo(pathBuffer);
        Span<byte> infoBuffer = stackalloc byte[(int)GxAbi.FileInfoAbiSize];
        int nativeResult;
        fixed (byte* pathPointer = pathBuffer)
        fixed (byte* infoPointer = infoBuffer)
        {
            nativeResult = _host->fileStat(
                _context, pathPointer, (uint)path.Length, infoPointer,
                GxAbi.FileInfoAbiSize);
        }
        if (nativeResult != 0) return (GuideXosFileResult)nativeResult;
        NativeFileInfo nativeInfo;
        fixed (byte* infoPointer = infoBuffer)
        {
            nativeInfo = *(NativeFileInfo*)infoPointer;
        }
        if (nativeInfo.type != 1u && nativeInfo.type != 2u)
        {
            return GuideXosFileResult.IoFailure;
        }
        info = new GuideXosFileInfo(
            nativeInfo.type == 2u ? GuideXosEntryType.Directory : GuideXosEntryType.Regular,
            nativeInfo.size);
        return GuideXosFileResult.Success;
    }

    internal NativeGxAppContext* Context => _context;
    internal NativeHostCallTable* HostTable => _host;

    private bool HasHostField(nuint offset)
    {
        return _host != null && _host->size >= offset + (nuint)sizeof(ulong);
    }
}

/// <summary>
/// Narrow C162 mutation wrapper. The caller supplies the full C160 identity;
/// ABI-v2 tables are rejected before the appended callback field is read.
/// </summary>
public static unsafe class GuideXosApplicationControl
{
    public static GuideXosApplicationCloseResult TryCloseApplication(
        GuideXosHost host, GuideXosApplicationInstanceId identity)
    {
        if (host == null) return GuideXosApplicationCloseResult.InvalidArgument;
        return TryCloseApplication(host.Context, host.HostTable, identity);
    }

    internal static GuideXosApplicationCloseResult TryCloseApplication(
        NativeGxAppContext* context, NativeHostCallTable* table,
        GuideXosApplicationInstanceId identity)
    {
        if (context == null || table == null || identity.Value == 0u ||
            identity.Source is not (
                GuideXosApplicationSnapshotSource.AppManagerInstance or
                GuideXosApplicationSnapshotSource.ShellSurface or
                GuideXosApplicationSnapshotSource.ManagedLogicalApplication))
        {
            return GuideXosApplicationCloseResult.InvalidArgument;
        }

        if (table->version < 3u || table->size < GxAbi.HostCallTableSize)
            return GuideXosApplicationCloseResult.NotSupported;
        if ((table->capabilities & GxAbi.CapabilityApplicationClose) == 0u)
            return GuideXosApplicationCloseResult.CapabilityUnavailable;
        if (table->closeApplication == null)
            return GuideXosApplicationCloseResult.NotSupported;

        int nativeResult = table->closeApplication(
            context, (uint)identity.Source, identity.Value);
        return nativeResult switch
        {
            0 => GuideXosApplicationCloseResult.Success,
            -2 => GuideXosApplicationCloseResult.InvalidArgument,
            -3 => GuideXosApplicationCloseResult.CapabilityUnavailable,
            -5 => GuideXosApplicationCloseResult.NotSupported,
            -10 => GuideXosApplicationCloseResult.NotFound,
            -11 => GuideXosApplicationCloseResult.Protected,
            -12 => GuideXosApplicationCloseResult.CloseFailed,
            -13 => GuideXosApplicationCloseResult.StaleIdentity,
            -14 => GuideXosApplicationCloseResult.Pending,
            _ => GuideXosApplicationCloseResult.NativeFailure,
        };
    }
}
