using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Bounded format and store tests for the C146 settings record.</summary>
public static class ManagedSettingsStoreC146Tests
{
    public static bool Run(GuideXosHost host)
    {
        int formatCases = 0;
        int storeCases = 0;
        bool result = true;
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        Span<byte> encoded = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];

        bool defaultEncoded = ManagedSettingsStore.TrySerialize(defaults,
            encoded, out int defaultLength) &&
            defaultLength == ManagedSettingsStore.EncodedFileBytes &&
            encoded[0] == (byte)'G' && encoded[1] == (byte)'X' &&
            encoded[2] == (byte)'S' && encoded[3] == (byte)'C' &&
            ManagedSettingsStore.TryDeserialize(encoded[..defaultLength],
                out ManagedSettingsSnapshot decodedDefault, out _) &&
            decodedDefault.Equals(defaults);
        result &= Case(ref formatCases, defaultEncoded);

        ManagedSettingsSnapshot nonDefault = new()
        {
            Density = 1,
            ShowStatus = false,
            ShowAdvanced = true,
            InputEnabled = false,
            NaturalScroll = true,
            ScrollSpeed = 2,
            ShowKeyboardTips = false,
            StatusDetail = 1,
            ReportFormat = 1,
        };
        bool nonDefaultRoundTrip = ManagedSettingsStore.TrySerialize(nonDefault,
            encoded, out int nonDefaultLength) &&
            ManagedSettingsStore.TryDeserialize(encoded[..nonDefaultLength],
                out ManagedSettingsSnapshot decodedNonDefault, out _) &&
            decodedNonDefault.Equals(nonDefault);
        result &= Case(ref formatCases, nonDefaultRoundTrip);

        Span<byte> valid = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        bool validSerialized = ManagedSettingsStore.TrySerialize(nonDefault,
            valid, out int validLength) && validLength == valid.Length;
        result &= Case(ref formatCases, validSerialized);
        result &= Case(ref formatCases, RejectWithMutation(valid,
            ManagedSettingsFileError.Magic, 0, (byte)'X'));
        result &= Case(ref formatCases, RejectWithMutation(valid,
            ManagedSettingsFileError.Version, 4, 2));
        result &= Case(ref formatCases, Reject(ManagedSettingsFileError.Header,
            valid[..8]));
        result &= Case(ref formatCases, Reject(ManagedSettingsFileError.Length,
            valid[..(valid.Length - 1)]));

        Span<byte> oversized = stackalloc byte[ManagedSettingsStore.MaximumFileBytes + 1];
        result &= Case(ref formatCases, Reject(ManagedSettingsFileError.Length, oversized));
        result &= Case(ref formatCases, RejectWithMutation(valid,
            ManagedSettingsFileError.Boolean, 13, 2, recomputeChecksum: true));
        result &= Case(ref formatCases, RejectWithMutation(valid,
            ManagedSettingsFileError.Enum, 12, 2, recomputeChecksum: true));

        Span<byte> trailing = stackalloc byte[valid.Length + 1];
        valid.CopyTo(trailing);
        result &= Case(ref formatCases, Reject(ManagedSettingsFileError.Length, trailing));
        result &= Case(ref formatCases, RejectWithMutation(valid,
            ManagedSettingsFileError.Checksum, ManagedSettingsStore.EncodedFileBytes - 1,
            0x80, xor: true));

        bool exactBoundAndDeterministic = valid.Length <= ManagedSettingsStore.MaximumFileBytes &&
            ManagedSettingsStore.TryDeserialize(valid, out ManagedSettingsSnapshot maxValue,
                out _) && maxValue.Equals(nonDefault);
        Span<byte> repeated = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        exactBoundAndDeterministic &= ManagedSettingsStore.TrySerialize(nonDefault,
            repeated, out int repeatedLength) && repeatedLength == valid.Length &&
            repeated.SequenceEqual(valid);
        result &= Case(ref formatCases, exactBoundAndDeterministic);
        var files = new MemorySettingsFiles();
        var store = new ManagedSettingsStore(files);
        ManagedSettingsLoadResult missing = store.Load();
        result &= Case(ref storeCases, missing.Status == ManagedSettingsLoadStatus.Missing &&
            missing.Snapshot.Equals(defaults) && files.ActiveHandles == 0);
        ManagedSettingsSaveStatus firstSave = store.Save(nonDefault);
        result &= Case(ref storeCases,
            firstSave == ManagedSettingsSaveStatus.Saved &&
            files.WriteCount == 1 && files.ActiveHandles == 0);
        ManagedSettingsLoadResult loaded = store.Load();
        result &= Case(ref storeCases, loaded.Status == ManagedSettingsLoadStatus.Loaded &&
            loaded.Snapshot.Equals(nonDefault) && loaded.FileSize == (ulong)valid.Length &&
            files.ActiveHandles == 0);

        ManagedSettingsSnapshot replacement = defaults;
        replacement.Density = 1;
        replacement.ReportFormat = 1;
        result &= Case(ref storeCases,
            store.Save(replacement) == ManagedSettingsSaveStatus.Saved &&
            store.Load().Snapshot.Equals(replacement) && files.WriteCount == 2 &&
            files.ActiveHandles == 0);

        files.SetCorruptFile();
        ManagedSettingsLoadResult corrupt = store.Load();
        result &= Case(ref storeCases, corrupt.Status == ManagedSettingsLoadStatus.Invalid &&
            corrupt.Snapshot.Equals(defaults) && files.ActiveHandles == 0);
        files.Clear();
        files.ReadResult = GuideXosFileResult.Success;
        result &= Case(ref storeCases,
            store.Save(nonDefault) == ManagedSettingsSaveStatus.Saved);

        files.WriteResult = GuideXosFileResult.IoFailure;
        result &= Case(ref storeCases,
            store.Save(replacement) == ManagedSettingsSaveStatus.IoFailure &&
            files.FileBytes.AsSpan(0, valid.Length).SequenceEqual(valid) && files.ActiveHandles == 0);
        files.WriteResult = GuideXosFileResult.Success;

        files.ReadResult = GuideXosFileResult.IoFailure;
        result &= Case(ref storeCases,
            store.Save(replacement) == ManagedSettingsSaveStatus.ReadBackMismatch &&
            files.ActiveHandles == 0 && !store.SaveInProgress);
        files.ReadResult = GuideXosFileResult.Success;

        int writesBeforeStress = files.WriteCount;
        bool repeatedSaves = true;
        for (int index = 0; index < 50; index++)
        {
            ManagedSettingsSnapshot candidate = Variant(index);
            ManagedSettingsLoadResult iterationLoad = default;
            repeatedSaves &= store.Save(candidate) == ManagedSettingsSaveStatus.Saved &&
                (iterationLoad = store.Load()).Status == ManagedSettingsLoadStatus.Loaded &&
                iterationLoad.Snapshot.Equals(candidate) && files.ActiveHandles == 0 &&
                files.FileBytes.Length <= ManagedSettingsStore.MaximumFileBytes;
        }
        repeatedSaves &= files.WriteCount == writesBeforeStress + 50 &&
            files.ActiveHandles == 0;
        result &= Case(ref storeCases, repeatedSaves);

        ManagedSettingsSaveStatus reentrantStatus = ManagedSettingsSaveStatus.Saved;
        files.OnWrite = () => reentrantStatus = store.Save(replacement);
        result &= Case(ref storeCases,
            store.Save(nonDefault) == ManagedSettingsSaveStatus.Saved &&
            reentrantStatus == ManagedSettingsSaveStatus.InProgress &&
            !store.SaveInProgress && files.ActiveHandles == 0);

        host?.TryLog(result && formatCases == 13
            ? "C146-FORMAT-TESTS cases=13 result=PASS"u8
            : "C146-FORMAT-TESTS result=FAIL"u8);
        host?.TryLog(result && storeCases == 10
            ? "C146-STORE-TESTS cases=10 stress=50 result=PASS"u8
            : "C146-STORE-TESTS result=FAIL"u8);
        return result && formatCases == 13 && storeCases == 10;
    }

    private static bool RejectWithMutation(ReadOnlySpan<byte> source,
        ManagedSettingsFileError expected, int offset, byte value,
        bool xor = false, bool recomputeChecksum = false)
    {
        Span<byte> copy = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        source.CopyTo(copy);
        copy[offset] = xor ? (byte)(copy[offset] ^ value) : value;
        if (recomputeChecksum) UpdateChecksum(copy);
        return Reject(expected, copy);
    }

    private static bool Reject(ManagedSettingsFileError expected,
        ReadOnlySpan<byte> source) =>
        !ManagedSettingsStore.TryDeserialize(source, out _, out ManagedSettingsFileError error) &&
        error == expected;

    private static void UpdateChecksum(Span<byte> bytes)
    {
        // The record checksum uses CRC-32/IEEE; compute it over header+payload.
        uint crc = 0xFFFFFFFFu;
        for (int index = 0; index < ManagedSettingsStore.EncodedFileBytes - 4; index++)
        {
            crc ^= bytes[index];
            for (int bit = 0; bit < 8; bit++)
            {
                uint mask = unchecked((uint)-(int)(crc & 1u));
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        crc = ~crc;
        int checksumOffset = ManagedSettingsStore.EncodedFileBytes - 4;
        bytes[checksumOffset] = (byte)crc;
        bytes[checksumOffset + 1] = (byte)(crc >> 8);
        bytes[checksumOffset + 2] = (byte)(crc >> 16);
        bytes[checksumOffset + 3] = (byte)(crc >> 24);
    }

    private static ManagedSettingsSnapshot Variant(int index) => new()
    {
        Density = index % 2,
        ShowStatus = (index & 1) == 0,
        ShowAdvanced = (index & 2) != 0,
        InputEnabled = (index & 4) == 0,
        NaturalScroll = (index & 8) != 0,
        ScrollSpeed = index % 3,
        ShowKeyboardTips = (index & 16) == 0,
        StatusDetail = (index >> 1) & 1,
        ReportFormat = (index >> 2) & 1,
    };

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }

    private sealed class MemorySettingsFiles : ManagedSettingsFileAccessBase
    {
        private readonly byte[] _fileBytes =
            new byte[ManagedSettingsStore.EncodedFileBytes];
        private int _fileLength;
        public byte[] FileBytes => _fileBytes;
        public bool Exists;
        public int ActiveHandles;
        public int WriteCount;
        public GuideXosFileResult ReadResult = GuideXosFileResult.Success;
        public GuideXosFileResult WriteResult = GuideXosFileResult.Success;
        public Action OnWrite;

        public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info)
        {
            info = null;
            if (!Exists) return GuideXosFileResult.NotFound;
            info = new GuideXosFileInfo(GuideXosEntryType.Regular,
                (ulong)_fileLength);
            return GuideXosFileResult.Success;
        }

        public override GuideXosFileResult ReadAllBytes(out byte[] data)
        {
            data = null;
            if (ReadResult != GuideXosFileResult.Success) return ReadResult;
            if (!Exists) return GuideXosFileResult.NotFound;
            ActiveHandles++;
            try { data = _fileBytes; }
            finally { ActiveHandles--; }
            return GuideXosFileResult.Success;
        }

        public void SetCorruptFile()
        {
            Exists = true;
            _fileLength = 3;
            _fileBytes[0] = (byte)'B';
            _fileBytes[1] = (byte)'A';
            _fileBytes[2] = (byte)'D';
        }

        public void Clear()
        {
            Exists = false;
            _fileLength = 0;
        }

        public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data)
        {
            Action callback = OnWrite;
            OnWrite = null;
            callback?.Invoke();
            if (WriteResult != GuideXosFileResult.Success) return WriteResult;
            if (data.Length != _fileBytes.Length)
                return GuideXosFileResult.InvalidArgument;
            data.CopyTo(_fileBytes);
            _fileLength = data.Length;
            Exists = true;
            WriteCount++;
            return GuideXosFileResult.Success;
        }
    }
}
