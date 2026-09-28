using System;

namespace HostLogProof.Applications;

internal enum ManagedSettingsFileError
{
    None,
    Header,
    Magic,
    Version,
    Length,
    Flags,
    Boolean,
    Enum,
    Checksum,
}

internal enum ManagedSettingsLoadStatus
{
    Missing,
    Loaded,
    Invalid,
    IoFailure,
}

internal enum ManagedSettingsSaveStatus
{
    Saved,
    InProgress,
    InvalidSnapshot,
    IoFailure,
    ReadBackMismatch,
}

internal readonly struct ManagedSettingsLoadResult
{
    public ManagedSettingsLoadResult(ManagedSettingsLoadStatus status,
        ManagedSettingsSnapshot snapshot, ulong fileSize,
        ManagedSettingsFileError formatError, GuideXosFileResult fileResult)
    {
        Status = status;
        Snapshot = snapshot;
        FileSize = fileSize;
        FormatError = formatError;
        FileResult = fileResult;
    }

    public ManagedSettingsLoadStatus Status { get; }
    public ManagedSettingsSnapshot Snapshot { get; }
    public ulong FileSize { get; }
    public ManagedSettingsFileError FormatError { get; }
    public GuideXosFileResult FileResult { get; }
}

internal interface IManagedSettingsFileAccess
{
    GuideXosFileResult TryGetInfo(out GuideXosFileInfo info);
    GuideXosFileResult ReadAllBytes(out byte[] data);
    GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data);
}

internal abstract class ManagedSettingsFileAccessBase : IManagedSettingsFileAccess
{
    public abstract GuideXosFileResult TryGetInfo(out GuideXosFileInfo info);
    public abstract GuideXosFileResult ReadAllBytes(out byte[] data);
    public abstract GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data);
}

/// <summary>
/// Versioned, fixed-width C146 settings format and bounded VFS store. The
/// store owns no UI state and only reports a candidate after read-back parses
/// to the exact snapshot that was written.
/// </summary>
internal sealed class ManagedSettingsStore
{
    public const int MaximumFileBytes = 64;
    public const int HeaderBytes = 12;
    public const int Version1PayloadBytes = 9;
    public const int Version1EncodedFileBytes = HeaderBytes + Version1PayloadBytes + 4;
    public const int PayloadBytes = 10;
    public const int ChecksumBytes = 4;
    public const int EncodedFileBytes = HeaderBytes + PayloadBytes + ChecksumBytes;
    public const ushort FormatVersion = 2;
    public const ushort LegacyFormatVersion = 1;
    public const int DefaultScrollLinesPerNotch = 3;
    public const int MinimumScrollLinesPerNotch = 1;
    public const int MaximumScrollLinesPerNotch = 8;

    private const uint Magic0 = (uint)'G';
    private const uint Magic1 = (uint)'X';
    private const uint Magic2 = (uint)'S';
    private const uint Magic3 = (uint)'C';
    public const int Version1ChecksumOffset = HeaderBytes + Version1PayloadBytes;
    private const int ChecksumOffset = HeaderBytes + PayloadBytes;

    private readonly ManagedSettingsFileAccessBase _files;
    private bool _saveInProgress;

    public ManagedSettingsStore(ManagedSettingsFileAccessBase files)
    {
        _files = files;
    }

    public bool SaveInProgress => _saveInProgress;

    public ManagedSettingsLoadResult Load()
    {
        if (_files == null)
        {
            return LoadFailure(ManagedSettingsLoadStatus.IoFailure,
                GuideXosFileResult.InvalidArgument);
        }

        GuideXosFileResult statResult = _files.TryGetInfo(out GuideXosFileInfo info);
        if (statResult == GuideXosFileResult.NotFound)
        {
            return new ManagedSettingsLoadResult(ManagedSettingsLoadStatus.Missing,
                ManagedSettingsSnapshot.Defaults, 0, ManagedSettingsFileError.None,
                GuideXosFileResult.NotFound);
        }
        if (statResult != GuideXosFileResult.Success || info == null)
        {
            return LoadFailure(ManagedSettingsLoadStatus.IoFailure, statResult);
        }
        if (info.Type != GuideXosEntryType.Regular || info.Size > MaximumFileBytes)
        {
            return LoadFailure(ManagedSettingsLoadStatus.Invalid,
                GuideXosFileResult.Success, info.Size, ManagedSettingsFileError.Length);
        }

        GuideXosFileResult readResult = _files.ReadAllBytes(out byte[] data);
        if (readResult != GuideXosFileResult.Success || data == null)
        {
            return LoadFailure(ManagedSettingsLoadStatus.IoFailure, readResult,
                info.Size);
        }
        if ((ulong)data.Length != info.Size || data.Length > MaximumFileBytes)
        {
            return LoadFailure(ManagedSettingsLoadStatus.Invalid,
                GuideXosFileResult.Success, info.Size, ManagedSettingsFileError.Length);
        }
        if (!TryDeserialize(data, out ManagedSettingsSnapshot snapshot,
                out ManagedSettingsFileError formatError))
        {
            return LoadFailure(ManagedSettingsLoadStatus.Invalid,
                GuideXosFileResult.Success, info.Size, formatError);
        }

        return new ManagedSettingsLoadResult(ManagedSettingsLoadStatus.Loaded,
            snapshot, info.Size, ManagedSettingsFileError.None,
            GuideXosFileResult.Success);
    }

    public ManagedSettingsSaveStatus Save(ManagedSettingsSnapshot candidate)
    {
        if (_saveInProgress) return ManagedSettingsSaveStatus.InProgress;
        if (_files == null || !IsValid(candidate))
            return ManagedSettingsSaveStatus.InvalidSnapshot;

        Span<byte> encoded = stackalloc byte[EncodedFileBytes];
        if (!TrySerialize(candidate, encoded, out int encodedLength) ||
            encodedLength != EncodedFileBytes)
        {
            return ManagedSettingsSaveStatus.InvalidSnapshot;
        }

        _saveInProgress = true;
        try
        {
            if (_files.WriteAllBytes(encoded) != GuideXosFileResult.Success)
                return ManagedSettingsSaveStatus.IoFailure;

            GuideXosFileResult statResult = _files.TryGetInfo(out GuideXosFileInfo info);
            if (statResult != GuideXosFileResult.Success || info == null ||
                info.Type != GuideXosEntryType.Regular ||
                info.Size != (ulong)encodedLength)
            {
                return ManagedSettingsSaveStatus.ReadBackMismatch;
            }

            GuideXosFileResult readResult = _files.ReadAllBytes(out byte[] readBack);
            if (readResult != GuideXosFileResult.Success || readBack == null ||
                readBack.Length != encodedLength)
            {
                return ManagedSettingsSaveStatus.ReadBackMismatch;
            }

            if (!TryDeserialize(readBack, out ManagedSettingsSnapshot actual,
                    out _))
            {
                return ManagedSettingsSaveStatus.ReadBackMismatch;
            }

            if (!actual.Equals(candidate))
            {
                return ManagedSettingsSaveStatus.ReadBackMismatch;
            }

            return ManagedSettingsSaveStatus.Saved;
        }
        finally
        {
            _saveInProgress = false;
        }
    }

    public static bool TrySerialize(ManagedSettingsSnapshot snapshot,
        Span<byte> destination, out int written)
    {
        written = 0;
        if (!IsValid(snapshot) || destination.Length < EncodedFileBytes)
            return false;

        destination[..EncodedFileBytes].Clear();
        destination[0] = (byte)Magic0;
        destination[1] = (byte)Magic1;
        destination[2] = (byte)Magic2;
        destination[3] = (byte)Magic3;
        WriteUInt16(destination, 4, FormatVersion);
        WriteUInt16(destination, 6, PayloadBytes);
        WriteUInt32(destination, 8, 0u); // Header flags remain zero in v1 and v2.

        int offset = HeaderBytes;
        destination[offset++] = (byte)snapshot.Density;
        destination[offset++] = snapshot.ShowStatus ? (byte)1 : (byte)0;
        destination[offset++] = snapshot.ShowAdvanced ? (byte)1 : (byte)0;
        destination[offset++] = snapshot.InputEnabled ? (byte)1 : (byte)0;
        destination[offset++] = snapshot.NaturalScroll ? (byte)1 : (byte)0;
        destination[offset++] = (byte)snapshot.ScrollSpeed;
        destination[offset++] = snapshot.ShowKeyboardTips ? (byte)1 : (byte)0;
        destination[offset++] = (byte)snapshot.StatusDetail;
        destination[offset++] = (byte)snapshot.ReportFormat;
        destination[offset] = (byte)snapshot.ScrollLinesPerNotch;

        WriteUInt32(destination, ChecksumOffset,
            ComputeCrc32(destination[..ChecksumOffset]));
        written = EncodedFileBytes;
        return true;
    }

    public static bool TryDeserialize(ReadOnlySpan<byte> source,
        out ManagedSettingsSnapshot snapshot, out ManagedSettingsFileError error)
    {
        snapshot = default;
        error = source.Length < HeaderBytes
            ? ManagedSettingsFileError.Header : ManagedSettingsFileError.Length;
        if (source.Length < HeaderBytes) return false;
        if (source[0] != (byte)Magic0 || source[1] != (byte)Magic1 ||
            source[2] != (byte)Magic2 || source[3] != (byte)Magic3)
        {
            error = ManagedSettingsFileError.Magic;
            return false;
        }
        ushort version = ReadUInt16(source, 4);
        if (version != LegacyFormatVersion && version != FormatVersion)
        {
            error = ManagedSettingsFileError.Version;
            return false;
        }

        int payloadBytes = version == LegacyFormatVersion
            ? Version1PayloadBytes : PayloadBytes;
        int encodedBytes = HeaderBytes + payloadBytes + ChecksumBytes;
        int checksumOffset = HeaderBytes + payloadBytes;
        if (source.Length != encodedBytes || ReadUInt16(source, 6) != payloadBytes)
        {
            error = ManagedSettingsFileError.Length;
            return false;
        }
        if (ReadUInt32(source, 8) != 0u)
        {
            error = ManagedSettingsFileError.Flags;
            return false;
        }
        if (ReadUInt32(source, checksumOffset) !=
            ComputeCrc32(source[..checksumOffset]))
        {
            error = ManagedSettingsFileError.Checksum;
            return false;
        }

        if (!TryReadVersion1Fields(source, out snapshot, out error)) return false;
        if (version == LegacyFormatVersion)
        {
            snapshot.ScrollLinesPerNotch = DefaultScrollLinesPerNotch;
            error = ManagedSettingsFileError.None;
            return true;
        }

        int offset = HeaderBytes + Version1PayloadBytes;
        byte scrollLinesPerNotch = source[offset];
        if (scrollLinesPerNotch < MinimumScrollLinesPerNotch ||
            scrollLinesPerNotch > MaximumScrollLinesPerNotch)
        {
            snapshot = default;
            error = ManagedSettingsFileError.Enum;
            return false;
        }
        snapshot.ScrollLinesPerNotch = scrollLinesPerNotch;
        error = ManagedSettingsFileError.None;
        return true;
    }

    private static bool TryReadVersion1Fields(ReadOnlySpan<byte> source,
        out ManagedSettingsSnapshot snapshot, out ManagedSettingsFileError error)
    {
        snapshot = default;
        int offset = HeaderBytes;
        byte density = source[offset++];
        if (density > 1)
        {
            error = ManagedSettingsFileError.Enum;
            return false;
        }
        if (!TryReadBoolean(source[offset++], out bool showStatus) ||
            !TryReadBoolean(source[offset++], out bool showAdvanced) ||
            !TryReadBoolean(source[offset++], out bool inputEnabled) ||
            !TryReadBoolean(source[offset++], out bool naturalScroll))
        {
            error = ManagedSettingsFileError.Boolean;
            return false;
        }
        byte scrollSpeed = source[offset++];
        if (scrollSpeed > 2)
        {
            error = ManagedSettingsFileError.Enum;
            return false;
        }
        if (!TryReadBoolean(source[offset++], out bool showKeyboardTips))
        {
            error = ManagedSettingsFileError.Boolean;
            return false;
        }
        byte statusDetail = source[offset++];
        byte reportFormat = source[offset];
        if (statusDetail > 1 || reportFormat > 1)
        {
            error = ManagedSettingsFileError.Enum;
            return false;
        }

        snapshot = new ManagedSettingsSnapshot
        {
            Density = density,
            ShowStatus = showStatus,
            ShowAdvanced = showAdvanced,
            InputEnabled = inputEnabled,
            NaturalScroll = naturalScroll,
            ScrollLinesPerNotch = DefaultScrollLinesPerNotch,
            ScrollSpeed = scrollSpeed,
            ShowKeyboardTips = showKeyboardTips,
            StatusDetail = statusDetail,
            ReportFormat = reportFormat,
        };
        error = ManagedSettingsFileError.None;
        return true;
    }

    public static bool IsValid(ManagedSettingsSnapshot snapshot) =>
        snapshot.Density is >= 0 and <= 1 &&
        snapshot.ScrollLinesPerNotch is >= MinimumScrollLinesPerNotch and
            <= MaximumScrollLinesPerNotch &&
        snapshot.ScrollSpeed is >= 0 and <= 2 &&
        snapshot.StatusDetail is >= 0 and <= 1 &&
        snapshot.ReportFormat is >= 0 and <= 1;

    private static ManagedSettingsLoadResult LoadFailure(
        ManagedSettingsLoadStatus status, GuideXosFileResult fileResult,
        ulong fileSize = 0, ManagedSettingsFileError formatError =
            ManagedSettingsFileError.None) =>
        new(status, ManagedSettingsSnapshot.Defaults, fileSize, formatError,
            fileResult);

    private static bool TryReadBoolean(byte value, out bool result)
    {
        result = value == 1;
        return value <= 1;
    }

    private static uint ComputeCrc32(ReadOnlySpan<byte> data)
    {
        uint crc = 0xFFFFFFFFu;
        for (int index = 0; index < data.Length; index++)
        {
            crc ^= data[index];
            for (int bit = 0; bit < 8; bit++)
            {
                uint mask = unchecked((uint)-(int)(crc & 1u));
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }

    private static ushort ReadUInt16(ReadOnlySpan<byte> source, int offset) =>
        (ushort)(source[offset] | (source[offset + 1] << 8));

    private static uint ReadUInt32(ReadOnlySpan<byte> source, int offset) =>
        (uint)(source[offset] | (source[offset + 1] << 8) |
            (source[offset + 2] << 16) | (source[offset + 3] << 24));

    private static void WriteUInt16(Span<byte> destination, int offset,
        ushort value)
    {
        destination[offset] = (byte)value;
        destination[offset + 1] = (byte)(value >> 8);
    }

    private static void WriteUInt32(Span<byte> destination, int offset,
        uint value)
    {
        destination[offset] = (byte)value;
        destination[offset + 1] = (byte)(value >> 8);
        destination[offset + 2] = (byte)(value >> 16);
        destination[offset + 3] = (byte)(value >> 24);
    }

}

internal sealed class ManagedSettingsVfsAccess : ManagedSettingsFileAccessBase
{
    private readonly GuideXosHost _host;

    public ManagedSettingsVfsAccess(GuideXosHost host)
    {
        _host = host;
    }

    public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info) =>
        GuideXosFile.TryGetInfo(_host, Path, out info);

    public override GuideXosFileResult ReadAllBytes(out byte[] data) =>
        GuideXosFile.ReadAllTextUtf8(_host, Path, out data);

    public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data) =>
        GuideXosFile.WriteAllTextUtf8(_host, Path, data);

    private static ReadOnlySpan<byte> Path => "/system/apps/GXSETT.BIN"u8;
}
