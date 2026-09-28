using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Focused startup, runtime-state, and real consumer tests for C147.</summary>
public static class GuideXosRuntimeSettingsC147Tests
{
    public static bool Run(GuideXosHost host)
    {
        bool startup = StartupCases(host, out int startupCases);
        bool state = RuntimeStateCases(out int stateCases);
        bool consumers = ConsumerCases(host, out int consumerCases);
        bool result = startup && state && consumers;
        Span<byte> summary = stackalloc byte[112];
        int summaryLength = 0;
        if (GuideXosText.Append(summary, ref summaryLength,
                "C147-FOCUSED-TESTS startup="u8) &&
            GuideXosText.Append(summary, ref summaryLength,
                startup ? "PASS"u8 : "FAIL"u8) &&
            GuideXosText.Append(summary, ref summaryLength, " runtime="u8) &&
            GuideXosText.Append(summary, ref summaryLength,
                state ? "PASS"u8 : "FAIL"u8) &&
            GuideXosText.Append(summary, ref summaryLength, " consumer="u8) &&
            GuideXosText.Append(summary, ref summaryLength,
                consumers ? "PASS"u8 : "FAIL"u8) &&
            GuideXosText.Append(summary, ref summaryLength, " result="u8) &&
            GuideXosText.Append(summary, ref summaryLength,
                result ? "PASS"u8 : "FAIL"u8))
        {
            host?.TryLog(summary[..summaryLength]);
        }
        host?.TryLog(startup && startupCases == 10
            ? "C147-STARTUP-TESTS cases=10 missing=corrupt-version-truncated once=PASS result=PASS"u8
            : "C147-STARTUP-TESTS result=FAIL"u8);
        host?.TryLog(state && stateCases == 15
            ? "C147-RUNTIME-STATE-TESTS cases=15 defaults=commit-reject-apply-failure-reset-discard result=PASS"u8
            : "C147-RUNTIME-STATE-TESTS result=FAIL"u8);
        host?.TryLog(consumers && consumerCases == 10
            ? "C147-CONSUMER-TESTS cases=10 TextArea=PASS ListBox=PASS lifecycle=PASS result=PASS"u8
            : "C147-CONSUMER-TESTS result=FAIL"u8);
        return result && startupCases == 10 && stateCases == 15 &&
            consumerCases == 10;
    }

    private static bool StartupCases(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool result = true;

        var emptyState = new GuideXosRuntimeSettingsState();
        result &= Case(ref cases, !emptyState.IsReady &&
            !emptyState.Current.NaturalScroll);

        var validFiles = new MemorySettingsFiles();
        var validStore = new ManagedSettingsStore(validFiles);
        ManagedSettingsSnapshot persisted = Variant(naturalScroll: true);
        bool saved = validStore.Save(persisted) == ManagedSettingsSaveStatus.Saved;
        int statsBeforeLoad = validFiles.StatCount;
        var loadedState = new GuideXosRuntimeSettingsState();
        var loadedStartup = new ManagedSettingsRuntimeStartup(loadedState);
        bool validLoad = saved && loadedStartup.Initialize(validStore) &&
            loadedStartup.LoadResult.Status == ManagedSettingsLoadStatus.Loaded &&
            loadedStartup.Snapshot.Equals(persisted) && loadedState.IsReady &&
            loadedState.Current.NaturalScroll;
        result &= Case(ref cases, validLoad);

        int statsAfterLoad = validFiles.StatCount;
        bool loadOnce = loadedStartup.Initialize(validStore) &&
            validFiles.StatCount == statsAfterLoad && statsAfterLoad == statsBeforeLoad + 1 &&
            validFiles.ActiveHandles == 0;
        result &= Case(ref cases, loadOnce);

        var missingFiles = new MemorySettingsFiles();
        var missingState = new GuideXosRuntimeSettingsState();
        var missingStartup = new ManagedSettingsRuntimeStartup(missingState);
        bool missing = missingStartup.Initialize(new ManagedSettingsStore(missingFiles)) &&
            missingStartup.LoadResult.Status == ManagedSettingsLoadStatus.Missing &&
            missingStartup.Snapshot.Equals(ManagedSettingsSnapshot.Defaults) &&
            missingState.IsReady && !missingState.Current.NaturalScroll &&
            missingFiles.WriteCount == 0;
        result &= Case(ref cases, missing);

        var corruptFiles = new MemorySettingsFiles();
        corruptFiles.SetBytes(new byte[] { (byte)'B', (byte)'A', (byte)'D' });
        var corruptState = new GuideXosRuntimeSettingsState();
        var corruptStartup = new ManagedSettingsRuntimeStartup(corruptState);
        bool corrupt = corruptStartup.Initialize(new ManagedSettingsStore(corruptFiles)) &&
            corruptStartup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            corruptStartup.Snapshot.Equals(ManagedSettingsSnapshot.Defaults) &&
            corruptState.IsReady && !corruptState.Current.NaturalScroll;
        result &= Case(ref cases, corrupt);

        var versionFiles = new MemorySettingsFiles();
        versionFiles.SetBytes(EncodedWithVersion(3));
        var versionState = new GuideXosRuntimeSettingsState();
        var versionStartup = new ManagedSettingsRuntimeStartup(versionState);
        bool unknownVersion = versionStartup.Initialize(new ManagedSettingsStore(versionFiles)) &&
            versionStartup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            versionStartup.LoadResult.FormatError == ManagedSettingsFileError.Version &&
            versionStartup.Snapshot.Equals(ManagedSettingsSnapshot.Defaults) &&
            versionState.IsReady && !versionState.Current.NaturalScroll;
        result &= Case(ref cases, unknownVersion);

        var truncatedFiles = new MemorySettingsFiles();
        truncatedFiles.SetBytes(EncodedWithVersion(1)[..24]);
        var truncatedState = new GuideXosRuntimeSettingsState();
        var truncatedStartup = new ManagedSettingsRuntimeStartup(truncatedState);
        bool truncated = truncatedStartup.Initialize(new ManagedSettingsStore(truncatedFiles)) &&
            truncatedStartup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            truncatedStartup.Snapshot.Equals(ManagedSettingsSnapshot.Defaults) &&
            truncatedState.IsReady;
        result &= Case(ref cases, truncated);

        bool corruptBootContinues = corruptStartup.IsInitialized &&
            versionStartup.IsInitialized && truncatedStartup.IsInitialized &&
            corruptFiles.ActiveHandles == 0 && versionFiles.ActiveHandles == 0 &&
            truncatedFiles.ActiveHandles == 0;
        result &= Case(ref cases, corruptBootContinues);

        result &= Case(ref cases,
            ManagedSettingsRuntimeStartup.ResolveSnapshot(
                missingStartup.LoadResult).Equals(
                    ManagedSettingsSnapshot.Defaults) &&
            !ManagedSettingsSnapshot.Defaults.NaturalScroll);
        result &= Case(ref cases, missingStartup.IsInitialized &&
            missingState.IsReady && missingFiles.ReadCount == 0);

        host?.TryLog(result && cases == 10
            ? "C147-STARTUP-DETAIL valid=file missing=defaults corrupt=defaults unsupported-version=defaults truncated=defaults ready=no-settings-center handles=closed result=PASS"u8
            : "C147-STARTUP-DETAIL result=FAIL"u8);
        return result && cases == 10;
    }

    private static bool RuntimeStateCases(out int cases)
    {
        cases = 0;
        bool result = true;
        var state = new GuideXosRuntimeSettingsState();
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, !state.IsReady &&
            !state.Current.NaturalScroll);

        ManagedSettingsSnapshot candidate = Variant(naturalScroll: true);
        result &= Case(ref cases, state.TryCommit(candidate) && state.IsReady &&
            state.Current.NaturalScroll);
        result &= Case(ref cases, state.TryCommit(candidate) &&
            state.Current.NaturalScroll);

        GuideXosRuntimeSettingsSnapshot prior = state.Current;
        ManagedSettingsSnapshot invalid = candidate;
        invalid.ScrollSpeed = 3;
        result &= Case(ref cases, !state.TryCommit(invalid) &&
            state.IsReady && state.Current.NaturalScroll == prior.NaturalScroll);

        ManagedSettingsSnapshot whole = candidate;
        whole.NaturalScroll = false;
        whole.Density = 1;
        result &= Case(ref cases, state.TryCommit(whole) &&
            !state.Current.NaturalScroll);
        result &= Case(ref cases, state.IsReady &&
            !state.Current.NaturalScroll);

        // A working edit, failed persistence, Reset, and Discard are all
        // local values until the verified-Apply commit path is called.
        ManagedSettingsSnapshot working = whole;
        working.NaturalScroll = true;
        result &= Case(ref cases, !state.Current.NaturalScroll);
        var files = new MemorySettingsFiles();
        var failureStore = new ManagedSettingsStore(files);
        var startup = new ManagedSettingsRuntimeStartup(state);
        result &= Case(ref cases, startup.Initialize(failureStore) &&
            !state.Current.NaturalScroll);
        ManagedSettingsSnapshot applied = startup.Snapshot;
        files.WriteResult = GuideXosFileResult.IoFailure;
        bool failedSave = startup.CanCommit(working) &&
            failureStore.Save(working) == ManagedSettingsSaveStatus.IoFailure;
        result &= Case(ref cases, failedSave && state.Current.NaturalScroll ==
            applied.NaturalScroll && startup.Snapshot.Equals(applied));

        ManagedSettingsSnapshot resetWorking = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, resetWorking.Equals(defaults) &&
            !state.Current.NaturalScroll && startup.Snapshot.Equals(applied));
        ManagedSettingsSnapshot discardWorking = working;
        result &= Case(ref cases, resetWorking.Equals(defaults) &&
            discardWorking.NaturalScroll && !state.Current.NaturalScroll &&
            startup.Snapshot.Equals(applied));
        result &= Case(ref cases, !state.Current.NaturalScroll &&
            startup.Snapshot.NaturalScroll == applied.NaturalScroll);

        files.WriteResult = GuideXosFileResult.Success;
        bool appliedAfterPersistence = startup.CanCommit(working) &&
            failureStore.Save(working) == ManagedSettingsSaveStatus.Saved;
        if (appliedAfterPersistence) startup.CommitPersisted(working);
        result &= Case(ref cases, appliedAfterPersistence &&
            state.Current.NaturalScroll && startup.Snapshot.Equals(working));
        result &= Case(ref cases, startup.CanCommit(working) &&
            state.IsReady && state.Current.NaturalScroll);
        ManagedSettingsLoadResult verified = failureStore.Load();
        result &= Case(ref cases, verified.Status == ManagedSettingsLoadStatus.Loaded &&
            verified.Snapshot.Equals(working));
        return result && cases == 15;
    }

    private static bool ConsumerCases(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool result = true;
        GuideXosRuntimeSettingsState state = GuideXosRuntimeSettings.Active;
        RuntimeSettingsTestCapture prior = state.CaptureForTests();
        try
        {
            ManagedSettingsSnapshot standard = ManagedSettingsSnapshot.Defaults;
            state.TryCommit(standard);
            GuideXosListBox list = NewListBox();
            GuideXosControlHost controlHost = NewListHost(list);
            bool standardDown = controlHost.HandleWheel(1, 8, 72, -1,
                0, 72, 8, 18) == GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 3;
            result &= ConsumerCase(host, ref cases, "standard"u8, standardDown);

            list.SetFirstVisibleIndex(0);
            ManagedSettingsSnapshot natural = standard;
            natural.NaturalScroll = true;
            state.TryCommit(natural);
            bool naturalDown = controlHost.HandleWheel(1, 8, 72, 1,
                0, 72, 8, 18) == GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 3;
            result &= ConsumerCase(host, ref cases, "natural"u8, naturalDown);

            list.SetFirstVisibleIndex(0);
            bool boundary = controlHost.HandleWheel(1, 8, 72, -1,
                0, 72, 8, 18) == GuideXosControlHostResult.Ignored &&
                list.FirstVisibleIndex == 0;
            result &= ConsumerCase(host, ref cases, "boundary"u8, boundary);

            state.TryCommit(standard);
            list.SetFirstVisibleIndex(0);
            ManagedSettingsSnapshot working = standard;
            working.NaturalScroll = true;
            bool unchangedBeforeApply = working.NaturalScroll &&
                controlHost.HandleWheel(1, 8, 72, 1, 0, 72, 8, 18) ==
                    GuideXosControlHostResult.Ignored &&
                list.FirstVisibleIndex == 0;
            result &= ConsumerCase(host, ref cases, "before-apply"u8,
                unchangedBeforeApply);

            bool applied = state.TryCommit(working);
            list.SetFirstVisibleIndex(0);
            bool immediate = applied &&
                controlHost.HandleWheel(1, 8, 72, 1, 0, 72, 8, 18) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 3;
            result &= ConsumerCase(host, ref cases, "immediate-apply"u8,
                immediate);

            // Closing/recreating the app host does not own or clear the
            // shared runtime value.
            list.SetFirstVisibleIndex(0);
            GuideXosControlHost relaunchedHost = NewListHost(list);
            bool survivesCloseAndRelaunch = state.Current.NaturalScroll &&
                relaunchedHost.HandleWheel(1, 8, 72, 1, 0, 72, 8, 18) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 3;
            result &= ConsumerCase(host, ref cases, "relaunch"u8,
                survivesCloseAndRelaunch);

            // Notes' TextArea uses the same shared control-host policy.
            var textArea = new GuideXosTextArea(256, 16, 4, 32);
            bool textLoaded = textArea.SetText(
                "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11");
            // SetText keeps the newly appended caret visible, which starts the
            // viewport at the bottom. Begin at the top so natural wheel-down
            // has room to move through the same shared host route.
            bool textViewportReady = textArea.SetFirstVisibleLine(0);
            var textHost = new GuideXosControlHost();
            GuideXosControlHostResult textRegistration =
                textHost.TryRegisterTextArea(2, textArea);
            GuideXosControlHostResult textWheel = textHost.HandleWheel(
                2, 8, 72, 1, 0, 72, 8, 18);
            bool textAreaEffect = textLoaded && textViewportReady &&
                textRegistration == GuideXosControlHostResult.Registered &&
                textWheel ==
                    GuideXosControlHostResult.Scrolled &&
                textArea.FirstVisibleLine == 3;
            result &= ConsumerCase(host, ref cases, "text-area"u8,
                textAreaEffect);

            ManagedSettingsSnapshot unrelated = working;
            unrelated.Density = 1;
            unrelated.ShowStatus = false;
            bool unrelatedCommitted = state.TryCommit(unrelated);
            list.SetFirstVisibleIndex(0);
            bool unrelatedSetting = unrelatedCommitted &&
                controlHost.HandleWheel(1, 8, 72, 1,
                0, 72, 8, 18) == GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 3;
            result &= ConsumerCase(host, ref cases, "unrelated-setting"u8,
                unrelatedSetting);

            ManagedSettingsSnapshot invalid = unrelated;
            invalid.Density = 7;
            GuideXosRuntimeSettingsSnapshot activeBeforeInvalid = state.Current;
            bool invalidRejected = !state.TryCommit(invalid) &&
                state.Current.NaturalScroll == activeBeforeInvalid.NaturalScroll;
            result &= ConsumerCase(host, ref cases, "invalid-rejected"u8,
                invalidRejected);

            var encoded = new byte[ManagedSettingsStore.EncodedFileBytes];
            bool invalidParserValue = !ManagedSettingsStore.TrySerialize(invalid,
                encoded, out _);
            result &= ConsumerCase(host, ref cases, "invalid-parser"u8,
                invalidParserValue);
        }
        finally
        {
            state.RestoreForTests(prior);
        }
        return result && cases == 10;
    }

    private static GuideXosListBox NewListBox()
    {
        var list = new GuideXosListBox(16, 24, 4, 24);
        for (int index = 0; index < 12; index++)
        {
            if (list.TryAdd("Row") != GuideXosListBoxPopulationResult.Added)
                return new GuideXosListBox(1, 24, 4, 24);
        }
        return list;
    }

    private static GuideXosControlHost NewListHost(GuideXosListBox list)
    {
        var host = new GuideXosControlHost();
        host.TryRegisterListBox(1, list);
        return host;
    }

    private static byte[] EncodedWithVersion(ushort version)
    {
        var bytes = new byte[ManagedSettingsStore.EncodedFileBytes];
        ManagedSettingsStore.TrySerialize(Variant(naturalScroll: true), bytes,
            out _);
        bytes[4] = (byte)version;
        bytes[5] = (byte)(version >> 8);
        return bytes;
    }

    private static ManagedSettingsSnapshot Variant(bool naturalScroll) => new()
    {
        Density = 0,
        ShowStatus = true,
        ShowAdvanced = false,
        InputEnabled = true,
        NaturalScroll = naturalScroll,
        ScrollLinesPerNotch = ManagedSettingsStore.DefaultScrollLinesPerNotch,
        ScrollSpeed = 1,
        ShowKeyboardTips = true,
        StatusDetail = 0,
        ReportFormat = 0,
    };

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }

    private static bool ConsumerCase(GuideXosHost host, ref int count,
        ReadOnlySpan<byte> name, bool value)
    {
        count++;
        Span<byte> line = stackalloc byte[80];
        int length = 0;
        if (GuideXosText.Append(line, ref length,
                "C147-CONSUMER-CASE name="u8) &&
            GuideXosText.Append(line, ref length, name) &&
            GuideXosText.Append(line, ref length,
                value ? " result=PASS"u8 : " result=FAIL"u8))
        {
            host?.TryLog(line[..length]);
        }
        return value;
    }

    private sealed class MemorySettingsFiles : ManagedSettingsFileAccessBase
    {
        private byte[] _bytes = new byte[ManagedSettingsStore.EncodedFileBytes];
        private int _length;

        public bool Exists { get; private set; }
        public int ActiveHandles { get; private set; }
        public int ReadCount { get; private set; }
        public int WriteCount { get; private set; }
        public int StatCount { get; private set; }
        public GuideXosFileResult WriteResult { get; set; } =
            GuideXosFileResult.Success;

        public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info)
        {
            StatCount++;
            info = null;
            if (!Exists) return GuideXosFileResult.NotFound;
            info = new GuideXosFileInfo(GuideXosEntryType.Regular,
                (ulong)_length);
            return GuideXosFileResult.Success;
        }

        public override GuideXosFileResult ReadAllBytes(out byte[] data)
        {
            data = null;
            if (!Exists) return GuideXosFileResult.NotFound;
            ActiveHandles++;
            ReadCount++;
            try
            {
                data = new byte[_length];
                Array.Copy(_bytes, data, _length);
            }
            finally
            {
                ActiveHandles--;
            }
            return GuideXosFileResult.Success;
        }

        public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data)
        {
            WriteCount++;
            if (WriteResult != GuideXosFileResult.Success) return WriteResult;
            _bytes = data.ToArray();
            _length = _bytes.Length;
            Exists = true;
            return GuideXosFileResult.Success;
        }

        public void SetBytes(byte[] bytes)
        {
            _bytes = bytes;
            _length = bytes.Length;
            Exists = true;
        }
    }
}
