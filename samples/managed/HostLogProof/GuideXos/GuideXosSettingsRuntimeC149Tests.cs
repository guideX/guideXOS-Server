#if HOSTLOGPROOF_C149_SECOND_RUNTIME_SETTING
using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Field audit and shared keyboard-tip runtime coverage for C149.</summary>
public static class GuideXosSettingsRuntimeC149Tests
{
    private const int PersistedSemanticFieldCount = 10;
    private const int RuntimeBackedFieldCount = 3;

    // Authentic C147 v1 fixture, SHA-256
    // 952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5.
    private static readonly byte[] s_authenticV1 =
    {
        0x47, 0x58, 0x53, 0x43, 0x01, 0x00, 0x09, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x00, 0x00, 0xB6, 0x1B, 0x6A, 0x36,
    };

    // Captured C148-era v2 proof record, SHA-256
    // 006409696A838F8E27AEC444BB23B51A4603687961CD3A38E5ECA2DF9B46CBB9.
    private static readonly byte[] s_c148V2 =
    {
        0x47, 0x58, 0x53, 0x43, 0x02, 0x00, 0x0A, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x00, 0x00, 0x07, 0x1C, 0x6F,
        0x1C, 0xAD,
    };

    public static bool Run(GuideXosHost host)
    {
        bool audit = FieldAudit(out int auditCases);
        bool runtime = RuntimeCases(out int runtimeCases);
        bool consumer = ConsumerCases(out int consumerCases);
        host?.TryLog(audit && auditCases == 14
            ? "C149-FIELD-AUDIT cases=14 fields=10 runtime-backed=3 v1-v2=PASS result=PASS"u8
            : "C149-FIELD-AUDIT result=FAIL"u8);
        host?.TryLog(runtime && runtimeCases == 14
            ? "C149-RUNTIME-TESTS cases=14 startup=PASS apply=PASS failure=PASS reset-discard-cancel=PASS result=PASS"u8
            : "C149-RUNTIME-TESTS result=FAIL"u8);
        host?.TryLog(consumer && consumerCases == 14
            ? "C149-CONSUMER-TESTS cases=14 tips-visible-hidden=PASS runtime-transition=PASS startup-fallback=PASS result=PASS"u8
            : "C149-CONSUMER-TESTS result=FAIL"u8);
        return audit && auditCases == 14 && runtime && runtimeCases == 14 &&
            consumer && consumerCases == 14;
    }

    private static bool FieldAudit(out int cases)
    {
        cases = 0;
        bool result = true;
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, PersistedSemanticFieldCount == 10 &&
            RuntimeBackedFieldCount == 3 && defaults.ShowKeyboardTips);

        ManagedSettingsSnapshot enabled = defaults;
        enabled.ShowKeyboardTips = true;
        ManagedSettingsSnapshot disabled = defaults;
        disabled.ShowKeyboardTips = false;
        result &= Case(ref cases, ManagedSettingsStore.IsValid(enabled) &&
            ManagedSettingsStore.IsValid(disabled));
        result &= Case(ref cases, !enabled.Equals(disabled));
        result &= Case(ref cases, enabled.GetHashCode() != disabled.GetHashCode());

        Span<byte> encodedEnabled = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        Span<byte> encodedDisabled = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        bool serializedEnabled = ManagedSettingsStore.TrySerialize(enabled,
            encodedEnabled, out int enabledLength);
        bool serializedDisabled = ManagedSettingsStore.TrySerialize(disabled,
            encodedDisabled, out int disabledLength);
        result &= Case(ref cases, serializedEnabled && serializedDisabled &&
            enabledLength == 26 && disabledLength == 26 &&
            encodedEnabled[4] == 2 && encodedEnabled[5] == 0 &&
            encodedEnabled[6] == 10 && encodedEnabled[7] == 0);
        result &= Case(ref cases, encodedEnabled[18] == 1 &&
            encodedDisabled[18] == 0);
        result &= Case(ref cases, ManagedSettingsStore.TryDeserialize(
            encodedEnabled, out ManagedSettingsSnapshot parsedEnabled, out _) &&
            parsedEnabled.Equals(enabled));
        result &= Case(ref cases, ManagedSettingsStore.TryDeserialize(
            encodedDisabled, out ManagedSettingsSnapshot parsedDisabled, out _) &&
            parsedDisabled.Equals(disabled));

        var c148Files = new MemorySettingsFiles();
        c148Files.SetBytes(s_c148V2);
        var c148Store = new ManagedSettingsStore(c148Files);
        ManagedSettingsLoadResult c148Load = c148Store.Load();
        result &= Case(ref cases, c148Load.Status == ManagedSettingsLoadStatus.Loaded &&
            c148Load.Snapshot.Density == 1 && !c148Load.Snapshot.ShowStatus &&
            c148Load.Snapshot.ShowAdvanced && c148Load.Snapshot.InputEnabled &&
            c148Load.Snapshot.NaturalScroll && c148Load.Snapshot.ScrollSpeed == 1 &&
            c148Load.Snapshot.ShowKeyboardTips &&
            c148Load.Snapshot.StatusDetail == 0 &&
            c148Load.Snapshot.ReportFormat == 0 &&
            c148Load.Snapshot.ScrollLinesPerNotch == 7 &&
            c148Files.WriteCount == 0);
        result &= Case(ref cases, c148Load.FileSize == 26 &&
            c148Files.Bytes.AsSpan().SequenceEqual(s_c148V2));

        var v1Files = new MemorySettingsFiles();
        v1Files.SetBytes(s_authenticV1);
        var v1Store = new ManagedSettingsStore(v1Files);
        ManagedSettingsLoadResult v1Load = v1Store.Load();
        result &= Case(ref cases, v1Load.Status == ManagedSettingsLoadStatus.Loaded &&
            v1Load.Snapshot.ShowKeyboardTips &&
            v1Load.Snapshot.ScrollLinesPerNotch ==
                ManagedSettingsStore.DefaultScrollLinesPerNotch &&
            v1Files.WriteCount == 0 &&
            v1Files.Bytes.AsSpan().SequenceEqual(s_authenticV1));
        result &= Case(ref cases, v1Load.Snapshot.Density == 1 &&
            !v1Load.Snapshot.ShowStatus && v1Load.Snapshot.ShowAdvanced &&
            v1Load.Snapshot.InputEnabled && v1Load.Snapshot.NaturalScroll &&
            v1Load.Snapshot.ScrollSpeed == 1 &&
            v1Load.Snapshot.StatusDetail == 0 &&
            v1Load.Snapshot.ReportFormat == 0);

        Span<byte> invalidV2 = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        encodedEnabled.CopyTo(invalidV2);
        invalidV2[18] = 2;
        RecomputeChecksum(invalidV2);
        result &= Case(ref cases, !ManagedSettingsStore.TryDeserialize(
            invalidV2, out _, out ManagedSettingsFileError invalidError) &&
            invalidError == ManagedSettingsFileError.Boolean);
        result &= Case(ref cases, encodedEnabled[18] == 1 &&
            encodedDisabled[18] == 0 && ManagedSettingsStore.FormatVersion == 2);
        return result;
    }

    private static bool RuntimeCases(out int cases)
    {
        cases = 0;
        bool result = true;
        var missingFiles = new MemorySettingsFiles();
        var missingState = new GuideXosRuntimeSettingsState();
        var missingStartup = new ManagedSettingsRuntimeStartup(missingState);
        bool missing = missingStartup.Initialize(
            new ManagedSettingsStore(missingFiles));
        result &= Case(ref cases, missing && missingStartup.LoadResult.Status ==
            ManagedSettingsLoadStatus.Missing && missingState.Current.ShowKeyboardTips &&
            missingState.Current.ScrollLinesPerNotch == 3);

        ManagedSettingsSnapshot appliedA = ManagedSettingsSnapshot.Defaults;
        var aFiles = FilesWith(appliedA);
        var aState = new GuideXosRuntimeSettingsState();
        var aStore = new ManagedSettingsStore(aFiles);
        var aStartup = new ManagedSettingsRuntimeStartup(aState);
        result &= Case(ref cases, aStartup.Initialize(aStore) &&
            aStartup.Snapshot.ShowKeyboardTips && aState.Current.ShowKeyboardTips);

        ManagedSettingsSnapshot bootB = ManagedSettingsSnapshot.Defaults;
        bootB.ShowKeyboardTips = false;
        var bState = new GuideXosRuntimeSettingsState();
        var bStartup = new ManagedSettingsRuntimeStartup(bState);
        result &= Case(ref cases, bStartup.Initialize(
                new ManagedSettingsStore(FilesWith(bootB))) &&
            !bStartup.Snapshot.ShowKeyboardTips && !bState.Current.ShowKeyboardTips);

        ManagedSettingsSnapshot workingB = aStartup.Snapshot;
        workingB.ShowKeyboardTips = false;
        bool dirty = !workingB.Equals(aStartup.Snapshot);
        result &= Case(ref cases, dirty && !workingB.ShowKeyboardTips &&
            aStartup.Snapshot.ShowKeyboardTips && aState.Current.ShowKeyboardTips &&
            aFiles.WriteCount == 0 &&
            !ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);

        ManagedSettingsSaveStatus success = aStore.Save(workingB);
        if (success == ManagedSettingsSaveStatus.Saved)
            aStartup.CommitPersisted(workingB);
        ManagedSettingsLoadResult persistedB = aStore.Load();
        result &= Case(ref cases, success == ManagedSettingsSaveStatus.Saved &&
            !aState.Current.ShowKeyboardTips && !aStartup.Snapshot.ShowKeyboardTips &&
            persistedB.Snapshot.Equals(workingB) &&
            ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);

        ManagedSettingsSnapshot workingA = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, !aState.Current.ShowKeyboardTips &&
            !persistedB.Snapshot.ShowKeyboardTips && workingA.ShowKeyboardTips &&
            !workingA.Equals(persistedB.Snapshot) &&
            ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);
        if (aStore.Save(workingA) == ManagedSettingsSaveStatus.Saved)
            aStartup.CommitPersisted(workingA);
        result &= Case(ref cases, aState.Current.ShowKeyboardTips &&
            aStartup.Snapshot.ShowKeyboardTips && aStore.Load().Snapshot.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);

        // Discard abandons working; Cancel retains it. Neither publishes it.
        workingB = aStartup.Snapshot;
        workingB.ShowKeyboardTips = false;
        ManagedSettingsSnapshot discardWorking = workingB;
        workingB = aStartup.Snapshot;
        result &= Case(ref cases, !discardWorking.ShowKeyboardTips &&
            workingB.ShowKeyboardTips && aState.Current.ShowKeyboardTips &&
            aStore.Load().Snapshot.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);
        workingB = aStartup.Snapshot;
        workingB.ShowKeyboardTips = false;
        ManagedSettingsSnapshot cancelWorking = workingB;
        result &= Case(ref cases, !cancelWorking.ShowKeyboardTips &&
            aStartup.Snapshot.ShowKeyboardTips && aState.Current.ShowKeyboardTips &&
            aStore.Load().Snapshot.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(aState.Current.ShowKeyboardTips).IsEmpty);

        // A reported write failure leaves runtime, applied and persisted A in
        // place while the caller retains the dirty B working copy.
        var failFiles = FilesWith(appliedA);
        var failStore = new ManagedSettingsStore(failFiles);
        var failState = new GuideXosRuntimeSettingsState();
        var failStartup = new ManagedSettingsRuntimeStartup(failState);
        bool failBoot = failStartup.Initialize(failStore);
        ManagedSettingsSnapshot failedWorking = failStartup.Snapshot;
        failedWorking.ShowKeyboardTips = false;
        failFiles.WriteResult = GuideXosFileResult.IoFailure;
        ManagedSettingsSaveStatus failedSave = failStore.Save(failedWorking);
        ManagedSettingsLoadResult afterFailure = failStore.Load();
        result &= Case(ref cases, failBoot &&
            failedSave == ManagedSettingsSaveStatus.IoFailure &&
            !failedWorking.Equals(failStartup.Snapshot) &&
            failStartup.Snapshot.ShowKeyboardTips && failState.Current.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(failState.Current.ShowKeyboardTips).IsEmpty &&
            afterFailure.Snapshot.ShowKeyboardTips &&
            afterFailure.Snapshot.Equals(failStartup.Snapshot));

        // Retry after removing failure publishes the same B only after save.
        failFiles.WriteResult = GuideXosFileResult.Success;
        bool retry = failStore.Save(failedWorking) == ManagedSettingsSaveStatus.Saved;
        if (retry) failStartup.CommitPersisted(failedWorking);
        result &= Case(ref cases, retry && !failState.Current.ShowKeyboardTips &&
            !failStartup.Snapshot.ShowKeyboardTips &&
            !failStore.Load().Snapshot.ShowKeyboardTips &&
            ManagedNotes.KeyboardTipsText(failState.Current.ShowKeyboardTips).IsEmpty);

        // An unrelated snapshot update preserves the selected field and the
        // immutable whole-snapshot commit retains wheel settings together.
        ManagedSettingsSnapshot unrelated = failedWorking;
        unrelated.NaturalScroll = true;
        unrelated.ScrollLinesPerNotch = 6;
        result &= Case(ref cases, failState.TryCommit(unrelated) &&
            !failState.Current.ShowKeyboardTips && failState.Current.NaturalScroll &&
            failState.Current.ScrollLinesPerNotch == 6);
        result &= Case(ref cases, failState.Current.ShowKeyboardTips ==
            unrelated.ShowKeyboardTips &&
            failState.Current.ScrollLinesPerNotch == unrelated.ScrollLinesPerNotch &&
            failState.Current.NaturalScroll == unrelated.NaturalScroll);

        ManagedSettingsSnapshot sameValue = unrelated;
        result &= Case(ref cases, failState.TryCommit(sameValue) &&
            failState.Current.ShowKeyboardTips == sameValue.ShowKeyboardTips &&
            failState.Current.NaturalScroll == sameValue.NaturalScroll &&
            failState.Current.ScrollLinesPerNotch == sameValue.ScrollLinesPerNotch);
        return result;
    }

    private static bool ConsumerCases(out int cases)
    {
        cases = 0;
        bool result = true;
        ReadOnlySpan<byte> shown = ManagedNotes.KeyboardTipsText(true);
        ReadOnlySpan<byte> hidden = ManagedNotes.KeyboardTipsText(false);
        result &= Case(ref cases, shown.SequenceEqual(
            "Tips: Tab/Shift+Tab focus, arrows navigate, Enter activates"u8));
        result &= Case(ref cases, hidden.IsEmpty);
        result &= Case(ref cases, !shown.SequenceEqual(hidden));
        result &= Case(ref cases,
            ManagedSettingsSnapshot.Defaults.ShowKeyboardTips && !shown.IsEmpty);

        var transition = new GuideXosRuntimeSettingsState();
        var transitionFiles = FilesWith(ManagedSettingsSnapshot.Defaults);
        var transitionStartup = new ManagedSettingsRuntimeStartup(transition);
        result &= Case(ref cases, transitionStartup.Initialize(
            new ManagedSettingsStore(transitionFiles)) &&
            !ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).IsEmpty);

        ManagedSettingsSnapshot candidateB = transitionStartup.Snapshot;
        candidateB.ShowKeyboardTips = false;
        result &= Case(ref cases, ManagedNotes.KeyboardTipsText(
                candidateB.ShowKeyboardTips).IsEmpty &&
            transition.Current.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).IsEmpty);
        if (transitionFiles.WriteResult == GuideXosFileResult.Success &&
            new ManagedSettingsStore(transitionFiles).Save(candidateB) ==
                ManagedSettingsSaveStatus.Saved)
        {
            transitionStartup.CommitPersisted(candidateB);
        }
        result &= Case(ref cases, transition.Current.ShowKeyboardTips == false &&
            ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).IsEmpty);

        // Reset stages default A without changing B; only a successful Apply
        // publishes the default and restores the Notes instruction.
        ManagedSettingsSnapshot resetA = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, !resetA.Equals(transitionStartup.Snapshot) &&
            !transition.Current.ShowKeyboardTips &&
            ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).IsEmpty);
        if (new ManagedSettingsStore(transitionFiles).Save(resetA) ==
            ManagedSettingsSaveStatus.Saved)
        {
            transitionStartup.CommitPersisted(resetA);
        }
        result &= Case(ref cases, transition.Current.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).IsEmpty);

        // A new Notes render reads the shared value after successful Apply;
        // this policy check does not claim the closed surface repaints live.
        result &= Case(ref cases, transition.Current.ShowKeyboardTips &&
            ManagedNotes.KeyboardTipsText(
                transition.Current.ShowKeyboardTips).SequenceEqual(shown));

        // Startup publishes persisted B before an application can render it.
        ManagedSettingsSnapshot startupB = ManagedSettingsSnapshot.Defaults;
        startupB.ShowKeyboardTips = false;
        var startupBState = new GuideXosRuntimeSettingsState();
        var startupBStartup = new ManagedSettingsRuntimeStartup(startupBState);
        result &= Case(ref cases, startupBStartup.Initialize(
                new ManagedSettingsStore(FilesWith(startupB))) &&
            ManagedNotes.KeyboardTipsText(
                startupBState.Current.ShowKeyboardTips).IsEmpty);

        var corruptFiles = FilesWith(startupB);
        corruptFiles.Bytes[22] ^= 1;
        var corruptState = new GuideXosRuntimeSettingsState();
        var corruptStartup = new ManagedSettingsRuntimeStartup(corruptState);
        result &= Case(ref cases, corruptStartup.Initialize(
                new ManagedSettingsStore(corruptFiles)) &&
            corruptStartup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            corruptState.Current.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(
                corruptState.Current.ShowKeyboardTips).IsEmpty);

        Span<byte> futureBytes = stackalloc byte[
            ManagedSettingsStore.EncodedFileBytes];
        ManagedSettingsStore.TrySerialize(startupB, futureBytes, out _);
        futureBytes[4] = 3;
        RecomputeChecksum(futureBytes);
        var futureFiles = new MemorySettingsFiles();
        futureFiles.SetBytes(futureBytes);
        var futureState = new GuideXosRuntimeSettingsState();
        var futureStartup = new ManagedSettingsRuntimeStartup(futureState);
        result &= Case(ref cases, futureStartup.Initialize(
                new ManagedSettingsStore(futureFiles)) &&
            futureStartup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            futureState.Current.ShowKeyboardTips &&
            !ManagedNotes.KeyboardTipsText(
                futureState.Current.ShowKeyboardTips).IsEmpty);

        ManagedSettingsSnapshot natural = ManagedSettingsSnapshot.Defaults;
        natural.NaturalScroll = true;
        natural.ScrollLinesPerNotch = 8;
        natural.ShowKeyboardTips = false;
        var composed = new GuideXosRuntimeSettingsState();
        result &= Case(ref cases, composed.TryCommit(natural) &&
            !composed.Current.ShowKeyboardTips &&
            composed.Current.NaturalScroll &&
            composed.Current.ScrollLinesPerNotch == 8 &&
            ManagedNotes.KeyboardTipsText(
                composed.Current.ShowKeyboardTips).IsEmpty);
        return result;
    }

    private static MemorySettingsFiles FilesWith(ManagedSettingsSnapshot snapshot)
    {
        Span<byte> record = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        ManagedSettingsStore.TrySerialize(snapshot, record, out _);
        var files = new MemorySettingsFiles();
        files.SetBytes(record);
        return files;
    }

    private static void RecomputeChecksum(Span<byte> record)
    {
        uint crc = 0xFFFFFFFFu;
        for (int index = 0; index < ManagedSettingsStore.HeaderBytes +
                ManagedSettingsStore.PayloadBytes; index++)
        {
            crc ^= record[index];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1u) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        crc = ~crc;
        int offset = ManagedSettingsStore.HeaderBytes +
            ManagedSettingsStore.PayloadBytes;
        record[offset] = (byte)crc;
        record[offset + 1] = (byte)(crc >> 8);
        record[offset + 2] = (byte)(crc >> 16);
        record[offset + 3] = (byte)(crc >> 24);
    }

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }

    private sealed class MemorySettingsFiles : ManagedSettingsFileAccessBase
    {
        private byte[] _bytes = Array.Empty<byte>();
        public bool Exists { get; private set; }
        public int WriteCount { get; private set; }
        public GuideXosFileResult WriteResult { get; set; } =
            GuideXosFileResult.Success;
        public byte[] Bytes => _bytes;

        public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info)
        {
            info = Exists
                ? new GuideXosFileInfo(GuideXosEntryType.Regular, (ulong)_bytes.Length)
                : null;
            return Exists ? GuideXosFileResult.Success : GuideXosFileResult.NotFound;
        }

        public override GuideXosFileResult ReadAllBytes(out byte[] data)
        {
            data = Exists ? _bytes.AsSpan().ToArray() : null;
            return Exists ? GuideXosFileResult.Success : GuideXosFileResult.NotFound;
        }

        public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data)
        {
            WriteCount++;
            if (WriteResult != GuideXosFileResult.Success) return WriteResult;
            _bytes = data.ToArray();
            Exists = true;
            return GuideXosFileResult.Success;
        }

        public void SetBytes(ReadOnlySpan<byte> bytes)
        {
            _bytes = bytes.ToArray();
            Exists = true;
        }
    }
}
#endif
