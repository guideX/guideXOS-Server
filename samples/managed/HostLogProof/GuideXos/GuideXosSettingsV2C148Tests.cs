using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Bounded v1-to-v2 migration and runtime scroll amount coverage.</summary>
public static class GuideXosSettingsV2C148Tests
{
    // Captured from the C147 sequence-02 test media after its verified Apply.
    // SHA-256: 952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5
    private static readonly byte[] s_authenticC147V1 =
    {
        0x47, 0x58, 0x53, 0x43, 0x01, 0x00, 0x09, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x00, 0x00, 0xB6, 0x1B, 0x6A, 0x36,
    };

    public static bool Run(GuideXosHost host)
    {
        bool format = FormatCases(out int formatCases);
        bool runtime = RuntimeCases(out int runtimeCases);
        bool consumer = ConsumerCases(host, out int consumerCases);
        host?.TryLog(format && formatCases == 23
            ? "C148-FORMAT-TESTS cases=23 v1-migration=PASS v2=PASS result=PASS"u8
            : "C148-FORMAT-TESTS result=FAIL"u8);
        host?.TryLog(runtime && runtimeCases == 17
            ? "C148-RUNTIME-STATE-TESTS cases=17 startup=PASS transactional-apply=PASS result=PASS"u8
            : "C148-RUNTIME-STATE-TESTS result=FAIL"u8);
        host?.TryLog(consumer && consumerCases == 15
            ? "C148-CONSUMER-TESTS cases=15 ListBox=PASS TextArea=PASS clamp=PASS result=PASS"u8
            : "C148-CONSUMER-TESTS result=FAIL"u8);
        host?.TryLog(format && runtime && consumer
            ? "C148-FOCUSED-TESTS format=PASS runtime=PASS consumer=PASS result=PASS"u8
            : "C148-FOCUSED-TESTS result=FAIL"u8);
        return format && formatCases == 23 && runtime && runtimeCases == 17 &&
            consumer && consumerCases == 15;
    }

    private static bool FormatCases(out int cases)
    {
        cases = 0;
        bool result = true;
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        ReadOnlySpan<byte> authentic = s_authenticC147V1;
        bool authenticFrame = authentic.Length ==
                ManagedSettingsStore.Version1EncodedFileBytes &&
            authentic[0] == (byte)'G' && authentic[1] == (byte)'X' &&
            authentic[2] == (byte)'S' && authentic[3] == (byte)'C' &&
            authentic[4] == 1 && authentic[5] == 0 &&
            authentic[6] == ManagedSettingsStore.Version1PayloadBytes &&
            authentic[7] == 0 && authentic[8] == 0 && authentic[9] == 0 &&
            authentic[10] == 0 && authentic[11] == 0;
        result &= Case(ref cases, authenticFrame);

        bool customTrue = ManagedSettingsStore.TryDeserialize(authentic,
            out ManagedSettingsSnapshot parsedTrue, out _) &&
            parsedTrue.Density == 1 && !parsedTrue.ShowStatus &&
            parsedTrue.ShowAdvanced && parsedTrue.InputEnabled &&
            parsedTrue.NaturalScroll && parsedTrue.ScrollSpeed == 1 &&
            parsedTrue.ShowKeyboardTips && parsedTrue.StatusDetail == 0 &&
            parsedTrue.ReportFormat == 0 &&
            parsedTrue.ScrollLinesPerNotch ==
                ManagedSettingsStore.DefaultScrollLinesPerNotch;
        result &= Case(ref cases, customTrue);

        Span<byte> falseCustom = stackalloc byte[
            ManagedSettingsStore.Version1EncodedFileBytes];
        CreateV1Variant(falseCustom, naturalScroll: false, useDefaults: false);
        bool customFalse = ManagedSettingsStore.TryDeserialize(falseCustom,
            out ManagedSettingsSnapshot parsedFalse, out _) &&
            parsedFalse.Density == parsedTrue.Density &&
            parsedFalse.ShowStatus == parsedTrue.ShowStatus &&
            parsedFalse.ShowAdvanced == parsedTrue.ShowAdvanced &&
            parsedFalse.InputEnabled == parsedTrue.InputEnabled &&
            !parsedFalse.NaturalScroll &&
            parsedFalse.ScrollSpeed == parsedTrue.ScrollSpeed &&
            parsedFalse.ShowKeyboardTips == parsedTrue.ShowKeyboardTips &&
            parsedFalse.StatusDetail == parsedTrue.StatusDetail &&
            parsedFalse.ReportFormat == parsedTrue.ReportFormat &&
            parsedFalse.ScrollLinesPerNotch ==
                ManagedSettingsStore.DefaultScrollLinesPerNotch;
        result &= Case(ref cases, customFalse);

        Span<byte> falseDefaults = stackalloc byte[
            ManagedSettingsStore.Version1EncodedFileBytes];
        CreateV1Variant(falseDefaults, naturalScroll: false, useDefaults: true);
        bool defaultV1 = ManagedSettingsStore.TryDeserialize(falseDefaults,
            out ManagedSettingsSnapshot parsedDefault, out _) &&
            parsedDefault.Equals(defaults);
        result &= Case(ref cases, defaultV1);

        var v1Files = new MemorySettingsFiles();
        v1Files.SetBytes(authentic);
        var v1Store = new ManagedSettingsStore(v1Files);
        ManagedSettingsLoadResult readOnlyLoad = v1Store.Load();
        bool readDoesNotRewrite = readOnlyLoad.Status ==
                ManagedSettingsLoadStatus.Loaded &&
            readOnlyLoad.FileSize == 25 && v1Files.WriteCount == 0 &&
            v1Files.Bytes.AsSpan().SequenceEqual(authentic);
        result &= Case(ref cases, readDoesNotRewrite);

        bool migratedTrue = v1Store.Save(readOnlyLoad.Snapshot) ==
                ManagedSettingsSaveStatus.Saved &&
            v1Files.Bytes.Length == ManagedSettingsStore.EncodedFileBytes &&
            v1Files.Bytes[4] == 2 && v1Files.Bytes[5] == 0 &&
            v1Files.Bytes[6] == ManagedSettingsStore.PayloadBytes &&
            v1Store.Load().Snapshot.Equals(parsedTrue) &&
            v1Files.WriteCount == 1;
        result &= Case(ref cases, migratedTrue);

        var falseFiles = new MemorySettingsFiles();
        falseFiles.SetBytes(falseCustom);
        var falseStore = new ManagedSettingsStore(falseFiles);
        ManagedSettingsLoadResult falseLoad = falseStore.Load();
        bool migratedFalse = falseLoad.Status == ManagedSettingsLoadStatus.Loaded &&
            !falseLoad.Snapshot.NaturalScroll &&
            falseLoad.Snapshot.ScrollLinesPerNotch == 3 &&
            falseStore.Save(falseLoad.Snapshot) == ManagedSettingsSaveStatus.Saved &&
            falseStore.Load().Snapshot.Equals(falseLoad.Snapshot) &&
            !falseStore.Load().Snapshot.NaturalScroll;
        result &= Case(ref cases, migratedFalse);

        Span<byte> encoded = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        bool v2Defaults = ManagedSettingsStore.TrySerialize(defaults,
                encoded, out int defaultLength) &&
            defaultLength == 26 && encoded[4] == 2 && encoded[5] == 0 &&
            encoded[6] == 10 && encoded[7] == 0 && encoded[21] == 3 &&
            ManagedSettingsStore.TryDeserialize(encoded[..defaultLength],
                out ManagedSettingsSnapshot decodedDefaults, out _) &&
            decodedDefaults.Equals(defaults);
        result &= Case(ref cases, v2Defaults);

        ManagedSettingsSnapshot customV2 = parsedTrue;
        customV2.ScrollLinesPerNotch = 5;
        bool v2Custom = ManagedSettingsStore.TrySerialize(customV2,
                encoded, out int customLength) && customLength == 26 &&
            encoded[21] == 5 &&
            ManagedSettingsStore.TryDeserialize(encoded[..customLength],
                out ManagedSettingsSnapshot decodedCustom, out _) &&
            decodedCustom.Equals(customV2);
        result &= Case(ref cases, v2Custom);

        customV2.ScrollLinesPerNotch = ManagedSettingsStore.MinimumScrollLinesPerNotch;
        bool minAmount = ManagedSettingsStore.TrySerialize(customV2,
            encoded, out _) && ManagedSettingsStore.TryDeserialize(encoded,
                out ManagedSettingsSnapshot minRead, out _) &&
            minRead.ScrollLinesPerNotch == 1;
        result &= Case(ref cases, minAmount);
        customV2.ScrollLinesPerNotch = ManagedSettingsStore.MaximumScrollLinesPerNotch;
        bool maxAmount = ManagedSettingsStore.TrySerialize(customV2,
            encoded, out _) && ManagedSettingsStore.TryDeserialize(encoded,
                out ManagedSettingsSnapshot maxRead, out _) &&
            maxRead.ScrollLinesPerNotch == 8;
        result &= Case(ref cases, maxAmount);

        customV2.ScrollLinesPerNotch = 0;
        bool zeroRejected = !ManagedSettingsStore.TrySerialize(customV2,
            encoded, out _);
        result &= Case(ref cases, zeroRejected);
        customV2.ScrollLinesPerNotch = 9;
        bool overMaxRejected = !ManagedSettingsStore.TrySerialize(customV2,
            encoded, out _);
        result &= Case(ref cases, overMaxRejected);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        bool truncated = !ManagedSettingsStore.TryDeserialize(
            encoded[..(encoded.Length - 1)], out _, out
            ManagedSettingsFileError truncatedError) &&
            truncatedError == ManagedSettingsFileError.Length;
        result &= Case(ref cases, truncated);

        Span<byte> mutated = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        encoded.CopyTo(mutated);
        mutated[6] = 9;
        UpdateChecksum(mutated, ManagedSettingsStore.HeaderBytes +
            ManagedSettingsStore.PayloadBytes);
        bool badPayload = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError payloadError) &&
            payloadError == ManagedSettingsFileError.Length;
        result &= Case(ref cases, badPayload);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        encoded.CopyTo(mutated);
        mutated[21] = 0;
        UpdateChecksum(mutated, 22);
        bool zeroFieldRejected = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError amountError) &&
            amountError == ManagedSettingsFileError.Enum;
        result &= Case(ref cases, zeroFieldRejected);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        encoded.CopyTo(mutated);
        mutated[21] = 9;
        UpdateChecksum(mutated, 22);
        bool oversizedFieldRejected = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError amountMaxError) &&
            amountMaxError == ManagedSettingsFileError.Enum;
        result &= Case(ref cases, oversizedFieldRejected);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        encoded.CopyTo(mutated);
        mutated[25] ^= 0x80;
        bool badChecksum = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError checksumError) &&
            checksumError == ManagedSettingsFileError.Checksum;
        result &= Case(ref cases, badChecksum);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        encoded.CopyTo(mutated);
        mutated[4] = 3;
        bool futureVersion = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError futureError) &&
            futureError == ManagedSettingsFileError.Version;
        result &= Case(ref cases, futureVersion);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        encoded.CopyTo(mutated);
        mutated[8] = 1;
        bool flagsRejected = !ManagedSettingsStore.TryDeserialize(mutated,
                out _, out ManagedSettingsFileError flagsError) &&
            flagsError == ManagedSettingsFileError.Flags;
        result &= Case(ref cases, flagsRejected);

        Span<byte> trailing = stackalloc byte[27];
        encoded.CopyTo(trailing);
        bool trailingRejected = !ManagedSettingsStore.TryDeserialize(trailing,
            out _, out ManagedSettingsFileError trailingError) &&
            trailingError == ManagedSettingsFileError.Length;
        result &= Case(ref cases, trailingRejected);

        Span<byte> oversized = stackalloc byte[
            ManagedSettingsStore.MaximumFileBytes + 1];
        bool oversizedRejected = !ManagedSettingsStore.TryDeserialize(oversized,
            out _, out _);
        result &= Case(ref cases, oversizedRejected);

        ManagedSettingsStore.TrySerialize(parsedTrue, encoded, out _);
        Span<byte> repeated = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        bool deterministic = ManagedSettingsStore.TrySerialize(parsedTrue,
                repeated, out int repeatedLength) && repeatedLength == encoded.Length &&
            repeated.SequenceEqual(encoded);
        result &= Case(ref cases, deterministic);
        return result && cases == 23;
    }

    private static bool RuntimeCases(out int cases)
    {
        cases = 0;
        bool result = true;
        var state = new GuideXosRuntimeSettingsState();
        ManagedSettingsSnapshot defaults = ManagedSettingsSnapshot.Defaults;
        result &= Case(ref cases, !state.IsReady &&
            state.Current.ScrollLinesPerNotch == 3);

        bool commitDefault = state.TryCommit(defaults) && state.IsReady &&
            state.Current.ScrollLinesPerNotch == 3 && !state.Current.NaturalScroll;
        result &= Case(ref cases, commitDefault);
        result &= Case(ref cases, state.TryCommit(defaults) &&
            state.Current.ScrollLinesPerNotch == 3);

        ManagedSettingsSnapshot candidate = defaults;
        candidate.ScrollLinesPerNotch = 1;
        bool minCommit = state.TryCommit(candidate) &&
            state.Current.ScrollLinesPerNotch == 1;
        result &= Case(ref cases, minCommit);
        candidate.ScrollLinesPerNotch = 8;
        bool maxCommit = state.TryCommit(candidate) &&
            state.Current.ScrollLinesPerNotch == 8;
        result &= Case(ref cases, maxCommit);

        GuideXosRuntimeSettingsSnapshot beforeInvalid = state.Current;
        candidate.ScrollLinesPerNotch = 0;
        bool zeroRejected = !state.TryCommit(candidate) &&
            state.Current.ScrollLinesPerNotch == beforeInvalid.ScrollLinesPerNotch;
        result &= Case(ref cases, zeroRejected);
        candidate.ScrollLinesPerNotch = 9;
        bool tooLargeRejected = !state.TryCommit(candidate) &&
            state.Current.ScrollLinesPerNotch == beforeInvalid.ScrollLinesPerNotch;
        result &= Case(ref cases, tooLargeRejected);

        candidate = defaults;
        candidate.NaturalScroll = false;
        candidate.ScrollLinesPerNotch = 5;
        bool standardDirection = state.TryCommit(candidate) &&
            !state.Current.NaturalScroll &&
            state.Current.NaturalScroll == false &&
            state.Current.ScrollLinesPerNotch == 5;
        result &= Case(ref cases, standardDirection);
        candidate.NaturalScroll = true;
        bool naturalDirection = state.TryCommit(candidate) &&
            state.Current.NaturalScroll &&
            state.Current.NaturalScroll &&
            state.Current.ScrollLinesPerNotch == 5;
        result &= Case(ref cases, naturalDirection);

        ManagedSettingsSnapshot working = candidate;
        working.ScrollLinesPerNotch = 2;
        bool workingIsolated = working.ScrollLinesPerNotch == 2 &&
            state.Current.ScrollLinesPerNotch == 5;
        result &= Case(ref cases, workingIsolated);
        bool apply = state.TryCommit(working) &&
            state.Current.ScrollLinesPerNotch == 2 && state.Current.NaturalScroll;
        result &= Case(ref cases, apply);

        ManagedSettingsSnapshot resetWorking = ManagedSettingsSnapshot.Defaults;
        bool resetIsolated = state.Current.ScrollLinesPerNotch == 2 &&
            resetWorking.ScrollLinesPerNotch == 3;
        result &= Case(ref cases, resetIsolated);

        var files = new MemorySettingsFiles();
        var store = new ManagedSettingsStore(files);
        ManagedSettingsSnapshot persisted = defaults;
        persisted.ScrollLinesPerNotch = 1;
        bool savedA = store.Save(persisted) == ManagedSettingsSaveStatus.Saved;
        var startupState = new GuideXosRuntimeSettingsState();
        var startup = new ManagedSettingsRuntimeStartup(startupState);
        bool loadedA = savedA && startup.Initialize(store) &&
            startupState.Current.ScrollLinesPerNotch == 1;
        byte[] bytesA = files.Bytes;
        ManagedSettingsSnapshot workingB = persisted;
        workingB.NaturalScroll = true;
        workingB.ScrollLinesPerNotch = 8;
        files.WriteResult = GuideXosFileResult.IoFailure;
        bool failedApply = loadedA && store.Save(workingB) ==
                ManagedSettingsSaveStatus.IoFailure &&
            startupState.Current.ScrollLinesPerNotch == 1 &&
            !startupState.Current.NaturalScroll &&
            startup.Snapshot.Equals(persisted) &&
            files.Bytes.AsSpan().SequenceEqual(bytesA) &&
            workingB.ScrollLinesPerNotch == 8;
        result &= Case(ref cases, failedApply);

        Span<byte> v1 = stackalloc byte[
            ManagedSettingsStore.Version1EncodedFileBytes];
        CreateV1Variant(v1, naturalScroll: true, useDefaults: false);
        var v1Files = new MemorySettingsFiles();
        v1Files.SetBytes(v1);
        var v1State = new GuideXosRuntimeSettingsState();
        var v1Startup = new ManagedSettingsRuntimeStartup(v1State);
        bool startupV1 = v1Startup.Initialize(new ManagedSettingsStore(v1Files)) &&
            v1Startup.LoadResult.Status == ManagedSettingsLoadStatus.Loaded &&
            v1State.Current.NaturalScroll && v1State.Current.ScrollLinesPerNotch == 3 &&
            v1Files.WriteCount == 0 && v1Files.Bytes.AsSpan().SequenceEqual(v1);
        result &= Case(ref cases, startupV1);

        ManagedSettingsSnapshot v2Value = defaults;
        v2Value.NaturalScroll = true;
        v2Value.ScrollLinesPerNotch = 5;
        Span<byte> v2 = stackalloc byte[ManagedSettingsStore.EncodedFileBytes];
        bool encodedV2 = ManagedSettingsStore.TrySerialize(v2Value, v2, out _);
        var v2Files = new MemorySettingsFiles();
        v2Files.SetBytes(v2);
        var v2State = new GuideXosRuntimeSettingsState();
        var v2Startup = new ManagedSettingsRuntimeStartup(v2State);
        bool startupV2 = encodedV2 && v2Startup.Initialize(
                new ManagedSettingsStore(v2Files)) &&
            v2State.Current.NaturalScroll &&
            v2State.Current.ScrollLinesPerNotch == 5;
        result &= Case(ref cases, startupV2);

        v2[21] = 0;
        UpdateChecksum(v2, 22);
        var badV2Files = new MemorySettingsFiles();
        badV2Files.SetBytes(v2);
        var badV2State = new GuideXosRuntimeSettingsState();
        var badV2Startup = new ManagedSettingsRuntimeStartup(badV2State);
        bool corruptDefaults = badV2Startup.Initialize(
                new ManagedSettingsStore(badV2Files)) &&
            badV2Startup.LoadResult.Status == ManagedSettingsLoadStatus.Invalid &&
            badV2State.IsReady && !badV2State.Current.NaturalScroll &&
            badV2State.Current.ScrollLinesPerNotch == 3;
        result &= Case(ref cases, corruptDefaults);

        bool defaultAfterReset = state.TryCommit(resetWorking) &&
            state.Current.ScrollLinesPerNotch == 3 && !state.Current.NaturalScroll;
        result &= Case(ref cases, defaultAfterReset);
        return result && cases == 17;
    }

    private static bool ConsumerCases(GuideXosHost host, out int cases)
    {
        cases = 0;
        bool result = true;
        GuideXosRuntimeSettingsState state = GuideXosRuntimeSettings.Active;
        RuntimeSettingsTestCapture prior = state.CaptureForTests();
        try
        {
            GuideXosListBox list = NewListBox(24);
            GuideXosControlHost controlHost = new();
            bool registered = controlHost.TryRegisterListBox(1, list) ==
                GuideXosControlHostResult.Registered;

            ManagedSettingsSnapshot settings = ManagedSettingsSnapshot.Defaults;
            settings.ScrollLinesPerNotch = 1;
            bool oneLine = registered && state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 17;
            result &= ConsumerCase(host, ref cases, "amount-1"u8, oneLine);

            settings.ScrollLinesPerNotch = 3;
            bool legacyDefault = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 19;
            result &= ConsumerCase(host, ref cases, "amount-default"u8,
                legacyDefault);

            settings.ScrollLinesPerNotch = 5;
            bool larger = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(10) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 15;
            result &= ConsumerCase(host, ref cases, "amount-5"u8, larger);

            settings.ScrollLinesPerNotch = 3;
            settings.NaturalScroll = false;
            bool standardDirection = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 19;
            result &= ConsumerCase(host, ref cases, "standard-direction"u8,
                standardDirection);

            settings.NaturalScroll = true;
            bool naturalDirection = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 13;
            result &= ConsumerCase(host, ref cases, "natural-direction"u8,
                naturalDirection);

            settings.ScrollLinesPerNotch = 5;
            bool naturalFive = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(10) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 5;
            result &= ConsumerCase(host, ref cases, "natural-five"u8,
                naturalFive);

            settings.NaturalScroll = false;
            bool twoNotches = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(8) &&
                controlHost.HandleWheel(1, 8, 72, -2) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 18;
            result &= ConsumerCase(host, ref cases, "two-notches"u8,
                twoNotches);

            settings.ScrollLinesPerNotch = 8;
            settings.NaturalScroll = true;
            bool topClamp = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(4) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 0;
            result &= ConsumerCase(host, ref cases, "top-clamp"u8, topClamp);

            settings.NaturalScroll = false;
            bool bottomClamp = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(list.MaximumFirstVisibleIndex - 4) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == list.MaximumFirstVisibleIndex;
            result &= ConsumerCase(host, ref cases, "bottom-clamp"u8,
                bottomClamp);

            settings.ScrollLinesPerNotch = 5;
            bool oldBeforeApply = state.TryCommit(settings) &&
                list.SetFirstVisibleIndex(10);
            ManagedSettingsSnapshot working = settings;
            working.ScrollLinesPerNotch = 1;
            oldBeforeApply &= working.ScrollLinesPerNotch == 1 &&
                state.Current.ScrollLinesPerNotch == 5 &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 15;
            result &= ConsumerCase(host, ref cases, "working-before-apply"u8,
                oldBeforeApply);

            bool afterApply = state.TryCommit(working) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 17;
            result &= ConsumerCase(host, ref cases, "after-apply"u8, afterApply);

            ManagedSettingsSnapshot unrelated = working;
            unrelated.Density = 1;
            bool unrelatedChange = state.TryCommit(unrelated) &&
                list.SetFirstVisibleIndex(16) &&
                controlHost.HandleWheel(1, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 17;
            result &= ConsumerCase(host, ref cases, "unrelated-setting"u8,
                unrelatedChange);

            list = NewListBox(24);
            list.SetFirstVisibleIndex(16);
            GuideXosControlHost relaunchedHost = new();
            bool relaunched = state.Current.ScrollLinesPerNotch == 1 &&
                relaunchedHost.TryRegisterListBox(2, list) ==
                    GuideXosControlHostResult.Registered &&
                relaunchedHost.HandleWheel(2, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                list.FirstVisibleIndex == 17;
            result &= ConsumerCase(host, ref cases, "close-relaunch"u8,
                relaunched);

            var textArea = new GuideXosTextArea(64, 32, 4, 32);
            bool textReady = textArea.SetText(
                "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n!") &&
                textArea.SetFirstVisibleLine(16);
            ManagedSettingsSnapshot textSettings = settings;
            textSettings.ScrollLinesPerNotch = 5;
            state.TryCommit(textSettings);
            var textHost = new GuideXosControlHost();
            bool textAreaMovement = textReady &&
                textHost.TryRegisterTextArea(3, textArea) ==
                    GuideXosControlHostResult.Registered &&
                textHost.HandleWheel(3, 8, 72, -1) ==
                    GuideXosControlHostResult.Scrolled &&
                textArea.FirstVisibleLine == 21;
            result &= ConsumerCase(host, ref cases, "text-area"u8,
                textAreaMovement);

            var shortList = NewListBox(3);
            var shortHost = new GuideXosControlHost();
            bool shortContent = shortHost.TryRegisterListBox(4, shortList) ==
                    GuideXosControlHostResult.Registered &&
                shortHost.HandleWheel(4, 8, 72, -1) ==
                    GuideXosControlHostResult.Ignored &&
                shortList.FirstVisibleIndex == 0;
            result &= ConsumerCase(host, ref cases, "short-content"u8,
                shortContent);
        }
        finally
        {
            state.RestoreForTests(prior);
        }
        return result && cases == 15;
    }

    private static GuideXosListBox NewListBox(int count)
    {
        var list = new GuideXosListBox(count, 3, 4, 24);
        for (int index = 0; index < count; index++)
        {
            if (list.TryAdd("Row") != GuideXosListBoxPopulationResult.Added)
                return list;
        }
        return list;
    }

    private static void CreateV1Variant(Span<byte> destination,
        bool naturalScroll, bool useDefaults)
    {
        s_authenticC147V1.AsSpan().CopyTo(destination);
        if (useDefaults)
        {
            destination[12] = 0;
            destination[13] = 1;
            destination[14] = 0;
            destination[15] = 1;
            destination[17] = 1;
            destination[18] = 1;
            destination[19] = 0;
            destination[20] = 0;
        }
        destination[16] = naturalScroll ? (byte)1 : (byte)0;
        UpdateChecksum(destination, ManagedSettingsStore.Version1ChecksumOffset);
    }

    private static void UpdateChecksum(Span<byte> bytes, int checksumOffset)
    {
        uint crc = ComputeCrc32(bytes[..checksumOffset]);
        bytes[checksumOffset] = (byte)crc;
        bytes[checksumOffset + 1] = (byte)(crc >> 8);
        bytes[checksumOffset + 2] = (byte)(crc >> 16);
        bytes[checksumOffset + 3] = (byte)(crc >> 24);
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

    private static bool Case(ref int cases, bool value)
    {
        cases++;
        return value;
    }

    private static bool ConsumerCase(GuideXosHost host, ref int cases,
        ReadOnlySpan<byte> name, bool value)
    {
        cases++;
        Span<byte> line = stackalloc byte[80];
        int length = 0;
        if (GuideXosText.Append(line, ref length, "C148-CONSUMER-CASE name="u8) &&
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
        private byte[] _bytes = Array.Empty<byte>();
        public int WriteCount { get; private set; }
        public GuideXosFileResult WriteResult = GuideXosFileResult.Success;

        public byte[] Bytes => _bytes;

        public void SetBytes(ReadOnlySpan<byte> bytes)
        {
            _bytes = bytes.ToArray();
        }

        public override GuideXosFileResult TryGetInfo(out GuideXosFileInfo info)
        {
            if (_bytes.Length == 0)
            {
                info = null;
                return GuideXosFileResult.NotFound;
            }
            info = new GuideXosFileInfo(GuideXosEntryType.Regular,
                (ulong)_bytes.Length);
            return GuideXosFileResult.Success;
        }

        public override GuideXosFileResult ReadAllBytes(out byte[] data)
        {
            data = (byte[])_bytes.Clone();
            return _bytes.Length == 0
                ? GuideXosFileResult.NotFound : GuideXosFileResult.Success;
        }

        public override GuideXosFileResult WriteAllBytes(ReadOnlySpan<byte> data)
        {
            if (WriteResult != GuideXosFileResult.Success) return WriteResult;
            _bytes = data.ToArray();
            WriteCount++;
            return GuideXosFileResult.Success;
        }
    }
}
