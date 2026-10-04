#if HOSTLOGPROOF_C160_APPLICATION_SNAPSHOT_PROOF
using System;

namespace HostLogProof;

internal static unsafe class GuideXosApplicationSnapshotC160Tests
{
    private static bool s_testsRun;
    private static uint s_failedCasesLow;
    private static uint s_failedCasesHigh;
    private static bool s_hasNotesIdentity;
    private static bool s_hasSettingsIdentity;
    private static bool s_hasCalculatorIdentity;
    private static bool s_hasTaskManagerIdentity;
    private static bool s_hasFileExplorerIdentity;
    private static GuideXosApplicationInstanceId s_notesIdentity;
    private static GuideXosApplicationInstanceId s_settingsIdentity;
    private static GuideXosApplicationInstanceId s_calculatorIdentity;
    private static GuideXosApplicationInstanceId s_taskManagerIdentity;
    private static GuideXosApplicationInstanceId s_fileExplorerIdentity;

    public static bool Run(GuideXosHost host, uint selector)
    {
        if (s_testsRun) return true;
        s_testsRun = true;
        s_failedCasesLow = 0u;
        s_failedCasesHigh = 0u;
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=begin"u8);
        int cases = 0;
        bool passed = true;
        NativeHostCallTable tableLayout = default;
        NativeHostCallTable* tablePointer = &tableLayout;
        NativeApplicationSnapshotRecord nativeRecordLayout = default;
        NativeApplicationSnapshotRecord* nativeRecordPointer = &nativeRecordLayout;
        GuideXosApplicationSnapshotRecord managedRecordLayout = default;
        GuideXosApplicationSnapshotRecord* managedRecordPointer = &managedRecordLayout;
        byte* nativeDisplayName = nativeRecordPointer->displayName;
        byte* nativeApplicationId = nativeRecordPointer->applicationId;
        byte* managedDisplayName = managedRecordPointer->displayName;
        byte* managedApplicationId = managedRecordPointer->applicationId;
        bool layout = sizeof(NativeHostCallTable) == GxAbi.HostCallTableSize &&
            FieldOffset(tablePointer, &tablePointer->version) == 4u &&
            FieldOffset(tablePointer, &tablePointer->log) == 8u &&
            FieldOffset(tablePointer, &tablePointer->requestWindow) == 16u &&
            FieldOffset(tablePointer, &tablePointer->drawText) == 24u &&
            FieldOffset(tablePointer, &tablePointer->drawRect) == 32u &&
            FieldOffset(tablePointer, &tablePointer->addButton) == 40u &&
            FieldOffset(tablePointer, &tablePointer->closeWindow) == 48u &&
            FieldOffset(tablePointer, &tablePointer->capabilities) == 56u &&
            FieldOffset(tablePointer, &tablePointer->addActionButton) == 64u &&
            FieldOffset(tablePointer, &tablePointer->fileReadAll) == 72u &&
            FieldOffset(tablePointer, &tablePointer->fileWriteAll) == 80u &&
            FieldOffset(tablePointer, &tablePointer->directoryList) == 88u &&
            FieldOffset(tablePointer, &tablePointer->fileStat) == 96u &&
            sizeof(NativeApplicationSnapshotRecord) ==
                GxAbi.ApplicationSnapshotRecordSize &&
            sizeof(GuideXosApplicationSnapshotRecord) ==
                GxAbi.ApplicationSnapshotRecordSize &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->recordVersion) == 0u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->source) == 4u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->instanceId) == 8u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->state) == 16u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->flags) == 20u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->displayNameLength) == 24u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->applicationIdLength) == 28u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->reserved0) == 32u &&
            FieldOffset(nativeRecordPointer,
                &nativeRecordPointer->reserved1) == 36u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->source) == 4u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->instanceId) == 8u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->state) == 16u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->flags) == 20u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->displayNameLength) == 24u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->applicationIdLength) == 28u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->reserved0) == 32u &&
            FieldOffset(managedRecordPointer, &managedRecordPointer->reserved1) == 36u &&
            FieldOffset(managedRecordPointer, managedDisplayName) == 40u &&
            FieldOffset(managedRecordPointer, managedApplicationId) == 72u &&
            FieldOffset(nativeRecordPointer, nativeDisplayName) == 40u &&
            FieldOffset(nativeRecordPointer, nativeApplicationId) == 72u &&
            FieldOffset(tablePointer, &tablePointer->applicationSnapshot) ==
                    GxAbi.ApplicationSnapshotOffset &&
            GxAbi.HostCallTableV1FullSize == GxAbi.ApplicationSnapshotOffset &&
            GxAbi.ApplicationSnapshotOffset + sizeof(ulong) ==
                GxAbi.HostCallTableV2Size &&
            FieldOffset(tablePointer, &tablePointer->closeApplication) ==
                GxAbi.ApplicationCloseOffset &&
            GxAbi.ApplicationCloseOffset + sizeof(ulong) ==
                GxAbi.HostCallTableSize &&
            sizeof(GuideXosApplicationSnapshot) ==
                16 + (int)GxAbi.ApplicationSnapshotBufferBytes;
        passed &= Case(ref cases, layout);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=layout"u8);

        GuideXosApplicationSnapshot emptySnapshot = default;
        passed &= Case(ref cases, emptySnapshot.TotalCount == 0u &&
            emptySnapshot.Count == 0u && !emptySnapshot.IsTruncated &&
            !emptySnapshot.TryGetRecord(0u, out _) &&
            GuideXosHost.IsValidApplicationSnapshotCountTuple(0, 0u, 0u));

        GuideXosApplicationInstanceId first = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance, 42u);
        GuideXosApplicationInstanceId same = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance, 42u);
        GuideXosApplicationInstanceId later = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance, 43u);
        GuideXosApplicationInstanceId otherSource = new(
            GuideXosApplicationSnapshotSource.ManagedLogicalApplication, 42u);
        passed &= Case(ref cases, first == same && first.GetHashCode() == same.GetHashCode());
        passed &= Case(ref cases, first != later);
        passed &= Case(ref cases, first != otherSource);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=identity"u8);

        NativeHostCallTable emptyTable = MakeV2Table();
        NativeGxAppContext emptyContext = MakeContext(&emptyTable);
        bool v2Accepted = host != null &&
            host.HasCapability(GuideXosCapability.ApplicationSnapshot) &&
            host.LaunchContext.Selector == selector;
        passed &= Case(ref cases, v2Accepted);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=v2-create"u8);

        NativeHostCallTable v1 = default;
        v1.size = GxAbi.HostCallTableV1FullSize;
        v1.version = GxAbi.HostAbiV1Version;
        v1.capabilities = (ulong)GuideXosCapability.Log;
        bool v1PrefixRecognized = v1.version == GxAbi.HostAbiV1Version &&
            v1.size == GxAbi.HostCallTableV1FullSize &&
            v1.size == GxAbi.ApplicationSnapshotOffset;
        NativeGxAppContext v1Context = default;
        v1Context.size = (uint)sizeof(NativeGxAppContext);
        v1Context.apiVersion = GxAbi.ApiVersion;
        v1Context.host = &v1;
        GuideXosApplicationSnapshotResult v1Feature =
            GuideXosHost.TryGetApplicationSnapshot(&v1Context, &v1,
                out GuideXosApplicationSnapshot v1Snapshot);
        passed &= Case(ref cases, v1PrefixRecognized);
        passed &= Case(ref cases, v1Feature ==
            GuideXosApplicationSnapshotResult.NotSupported && v1Snapshot.Count == 0u);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=v1-prefix"u8);

        NativeHostCallTable malformedV2Short = MakeV2Table();
        malformedV2Short.size = GxAbi.HostCallTableV1FullSize;
        NativeGxAppContext malformedV2Context = MakeContext(&malformedV2Short);
        passed &= Case(ref cases,
            GuideXosHost.TryGetApplicationSnapshot(&malformedV2Context,
                &malformedV2Short, out _) ==
                GuideXosApplicationSnapshotResult.NotSupported);

        NativeHostCallTable malformedV1Long = MakeV2Table();
        malformedV1Long.version = GxAbi.HostAbiV1Version;
        NativeGxAppContext malformedV1Context = MakeContext(&malformedV1Long);
        passed &= Case(ref cases,
            GuideXosHost.TryGetApplicationSnapshot(&malformedV1Context,
                &malformedV1Long, out _) ==
                GuideXosApplicationSnapshotResult.NotSupported);

        NativeHostCallTable unknown = MakeV2Table();
        unknown.version = 99u;
        NativeGxAppContext unknownContext = MakeContext(&unknown);
        passed &= Case(ref cases,
            GuideXosHost.TryGetApplicationSnapshot(&unknownContext,
                &unknown, out _) == GuideXosApplicationSnapshotResult.NotSupported);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=malformed-versions"u8);

        NativeHostCallTable noCapability = MakeV2Table();
        noCapability.capabilities = 0u;
        NativeGxAppContext noCapabilityContext = MakeContext(&noCapability);
        passed &= Case(ref cases,
            GuideXosHost.TryGetApplicationSnapshot(&noCapabilityContext,
                &noCapability, out _) ==
                GuideXosApplicationSnapshotResult.CapabilityUnavailable);

        NativeHostCallTable nullCallback = MakeV2Table();
        nullCallback.applicationSnapshot = null;
        NativeGxAppContext nullCallbackContext = MakeContext(&nullCallback);
        passed &= Case(ref cases,
            GuideXosHost.TryGetApplicationSnapshot(&nullCallbackContext,
                &nullCallback, out _) ==
                GuideXosApplicationSnapshotResult.NotSupported);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=capability"u8);

        passed &= Case(ref cases, GuideXosHost.TryGetApplicationSnapshot(
            &emptyContext, &emptyTable,
            GxAbi.ApplicationSnapshotCapacity + 1u, out _) ==
            GuideXosApplicationSnapshotResult.InvalidArgument);
        passed &= Case(ref cases,
            GuideXosHost.IsValidApplicationSnapshotCountTuple(
                0, GxAbi.ApplicationSnapshotCapacity,
                GxAbi.ApplicationSnapshotCapacity) &&
            GuideXosHost.IsValidApplicationSnapshotCountTuple(
                1, GxAbi.ApplicationSnapshotCapacity,
                GxAbi.ApplicationSnapshotCapacity - 1u) &&
            !GuideXosHost.IsValidApplicationSnapshotCountTuple(0, 1u, 0u) &&
            !GuideXosHost.IsValidApplicationSnapshotCountTuple(0, 1u, 2u) &&
            !GuideXosHost.IsValidApplicationSnapshotCountTuple(
                0, GxAbi.ApplicationSnapshotCapacity + 1u,
                GxAbi.ApplicationSnapshotCapacity + 1u) &&
            !GuideXosHost.IsValidApplicationSnapshotCountTuple(1, 1u, 1u));
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=abi"u8);

        GuideXosApplicationSnapshotResult productionResult =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot productionSnapshot);
        GuideXosApplicationSnapshotResult repeatResult =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot productionRepeat);
        passed &= Case(ref cases, productionResult ==
            GuideXosApplicationSnapshotResult.Success && productionSnapshot.Count > 0u);
        passed &= Case(ref cases, repeatResult ==
            GuideXosApplicationSnapshotResult.Success &&
            SameSnapshot(productionSnapshot, productionRepeat));
        passed &= Case(ref cases,
            HasActiveManagedIdentity(productionSnapshot, selector));
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=live"u8);

        GuideXosApplicationSnapshotResult zeroCapacityResult =
            host.TryGetApplicationSnapshot(0u,
                out GuideXosApplicationSnapshot zeroCapacitySnapshot);
        passed &= Case(ref cases, zeroCapacityResult ==
            GuideXosApplicationSnapshotResult.Truncated &&
            zeroCapacitySnapshot.TotalCount == productionSnapshot.TotalCount &&
            zeroCapacitySnapshot.Count == 0u && zeroCapacitySnapshot.IsTruncated);
        GuideXosApplicationSnapshotResult oneCapacityResult =
            host.TryGetApplicationSnapshot(1u,
                out GuideXosApplicationSnapshot oneCapacitySnapshot);
        passed &= Case(ref cases, oneCapacityResult ==
            GuideXosApplicationSnapshotResult.Truncated &&
            oneCapacitySnapshot.TotalCount == productionSnapshot.TotalCount &&
            oneCapacitySnapshot.Count == 1u && oneCapacitySnapshot.IsTruncated &&
            SameFirstRecord(productionSnapshot, oneCapacitySnapshot));
        bool appManagerRows = false;
        bool nativeCalculator = false;
        bool nativeTaskManager = false;
        bool shellSurface = false;
        for (uint index = 0u; index < productionSnapshot.Count; ++index)
        {
            if (!productionSnapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record)) continue;
            if (record.source == GuideXosApplicationSnapshotSource.AppManagerInstance)
            {
                appManagerRows = true;
                nativeCalculator |= ApplicationIdEquals(ref record,
                    "gxos.builtin.calculator"u8);
                nativeTaskManager |= ApplicationIdEquals(ref record,
                    "gxos.builtin.taskmanager"u8);
            }
            shellSurface |= record.source == GuideXosApplicationSnapshotSource.ShellSurface;
        }
        passed &= Case(ref cases, appManagerRows && nativeCalculator &&
            nativeTaskManager && shellSurface);

        bool stress = true;
        GuideXosApplicationSnapshot baseline = productionSnapshot;
        for (int call = 0; call < 1000; ++call)
        {
            if (host.TryGetApplicationSnapshot(
                    out GuideXosApplicationSnapshot current) !=
                    GuideXosApplicationSnapshotResult.Success ||
                !SameSnapshot(baseline, current))
            {
                stress = false;
                break;
            }
        }
        passed &= Case(ref cases, stress);
        host.TryLog("C160-MANAGED-SNAPSHOT-PHASE=stress"u8);

        if (passed && cases >= 20)
        {
            Span<byte> line = stackalloc byte[192];
            int position = 0;
            Append(line, ref position, "C160-MANAGED-SNAPSHOT-TESTS cases="u8);
            AppendUnsigned(line, ref position, (uint)cases);
            Append(line, ref position,
                " v1=NotSupported malformed=Rejected layout=PASS identity=PASS stress=1000 result=PASS"u8);
            host.TryLog(line[..position]);
        }
        else
        {
            Span<byte> failureLine = stackalloc byte[96];
            int failurePosition = 0;
            Append(failureLine, ref failurePosition,
                "C160-MANAGED-SNAPSHOT-TESTS result=FAIL failed="u8);
            AppendUnsigned(failureLine, ref failurePosition, s_failedCasesLow);
            Append(failureLine, ref failurePosition, ":"u8);
            AppendUnsigned(failureLine, ref failurePosition, s_failedCasesHigh);
            host.TryLog(failureLine[..failurePosition]);
        }
        return passed && cases >= 20;
    }

    public static bool VerifyProductionLaunch(GuideXosHost host,
        uint selector, out GuideXosApplicationSnapshot snapshot,
        out GuideXosApplicationInstanceId managedIdentity)
    {
        managedIdentity = default;
        snapshot = default;
        if (host == null || host.TryGetApplicationSnapshot(out snapshot) !=
                GuideXosApplicationSnapshotResult.Success ||
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot repeated) !=
                GuideXosApplicationSnapshotResult.Success ||
            !SameSnapshot(snapshot, repeated)) return false;

        ReadOnlySpan<byte> expectedApplicationId = selector switch
        {
            4u => "com.guidexos.apps.managed.notes"u8,
            5u => "com.guidexos.apps.managed.settingscenter"u8,
            6u => "com.guidexos.apps.managed.calculator"u8,
            7u => "com.guidexos.apps.managed.taskmanager"u8,
            8u => "com.guidexos.apps.managed.fileexplorer"u8,
            _ => ReadOnlySpan<byte>.Empty,
        };
        bool selfFound = false;
        uint activeCount = 0u;
        for (uint index = 0u; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record)) return false;
            if (record.IsActive) ++activeCount;
            if (record.source != GuideXosApplicationSnapshotSource.ManagedLogicalApplication)
                continue;
            if (ApplicationIdEquals(ref record, expectedApplicationId))
            {
                if (!record.IsActive) return false;
                managedIdentity = record.Identity;
                selfFound = true;
            }
            else if (record.IsActive)
            {
                return false;
            }
        }

        bool transition = selfFound && activeCount == 1u && expectedApplicationId.Length != 0;
        bool replacingKnownIdentity = false;
        if (transition && TryGetPreviousManagedIdentity(selector,
                out GuideXosApplicationInstanceId previousIdentity))
        {
            replacingKnownIdentity = true;
            transition = managedIdentity != previousIdentity;
            for (uint index = 0u; transition && index < snapshot.Count; ++index)
            {
                if (snapshot.TryGetRecord(index,
                        out GuideXosApplicationSnapshotRecord record) &&
                    record.Identity == previousIdentity)
                    transition = false;
            }
        }
        if (transition)
        {
            SetManagedIdentity(selector, managedIdentity);
            LogProductionIdentity(host, selector,
                managedIdentity, snapshot.TotalCount, activeCount,
                replacingKnownIdentity);
        }
        return transition;
    }

    public static bool VerifyProductionDispatch(GuideXosHost host, uint selector)
    {
        if (host == null || !TryGetPreviousManagedIdentity(selector,
                out GuideXosApplicationInstanceId expectedIdentity) ||
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot) !=
                GuideXosApplicationSnapshotResult.Success ||
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot repeated) !=
                GuideXosApplicationSnapshotResult.Success ||
            !SameSnapshot(snapshot, repeated)) return false;

        ReadOnlySpan<byte> expectedApplicationId = selector switch
        {
            4u => "com.guidexos.apps.managed.notes"u8,
            5u => "com.guidexos.apps.managed.settingscenter"u8,
            6u => "com.guidexos.apps.managed.calculator"u8,
            7u => "com.guidexos.apps.managed.taskmanager"u8,
            8u => "com.guidexos.apps.managed.fileexplorer"u8,
            _ => ReadOnlySpan<byte>.Empty,
        };
        if (expectedApplicationId.IsEmpty) return false;
        uint activeCount = 0u;
        bool found = false;
        for (uint index = 0u; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record)) return false;
            if (record.IsActive) ++activeCount;
            if (record.source != GuideXosApplicationSnapshotSource.ManagedLogicalApplication)
                continue;
            if (record.Identity == expectedIdentity)
            {
                if (!ApplicationIdEquals(ref record, expectedApplicationId)) return false;
                found = true;
            }
        }
        // Several managed surfaces may remain live; only the focused one has
        // the active flag. Background-surface dispatch still validates its
        // exact lifetime and the snapshot's single-active invariant.
        return found && activeCount == 1u;
    }

    public static bool VerifyProductionClose(GuideXosHost host, uint selector)
    {
        if (selector != 8u || host == null ||
            !TryGetPreviousManagedIdentity(selector,
                out GuideXosApplicationInstanceId expectedIdentity) ||
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot) !=
                GuideXosApplicationSnapshotResult.Success ||
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot repeated) !=
                GuideXosApplicationSnapshotResult.Success ||
            !SameSnapshot(snapshot, repeated)) return false;

        uint activeCount = 0u;
        for (uint index = 0u; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record)) return false;
            if (record.IsActive) ++activeCount;
            if (record.Identity == expectedIdentity) return false;
        }
        if (activeCount != 1u) return false;

        Span<byte> line = stackalloc byte[112];
        int position = 0;
        Append(line, ref position, "C160-SNAPSHOT id=fileexplorer source=3 instance="u8);
        AppendUnsigned64(line, ref position, expectedIdentity.Value);
        Append(line, ref position, " closed=1 result=PASS"u8);
        host.TryLog(line[..position]);
        return true;
    }

    private static NativeHostCallTable MakeV2Table()
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableV2Size;
        table.version = GxAbi.HostAbiV2Version;
        table.capabilities = GxAbi.CapabilityApplicationSnapshot;
        return table;
    }

    private static NativeGxAppContext MakeContext(NativeHostCallTable* table)
    {
        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        return context;
    }

    private static bool SameSnapshot(GuideXosApplicationSnapshot left,
        GuideXosApplicationSnapshot right)
    {
        if (left.TotalCount != right.TotalCount || left.Count != right.Count ||
            left.IsTruncated != right.IsTruncated) return false;
        for (uint index = 0u; index < left.Count; ++index)
        {
            if (!left.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord leftRecord) ||
                !right.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord rightRecord) ||
                leftRecord.Identity != rightRecord.Identity ||
                leftRecord.state != rightRecord.state ||
                leftRecord.flags != rightRecord.flags ||
                leftRecord.displayNameLength != rightRecord.displayNameLength ||
                leftRecord.applicationIdLength != rightRecord.applicationIdLength ||
                !SameRecord(ref leftRecord, ref rightRecord)) return false;
        }
        return true;
    }

    private static bool SameFirstRecord(GuideXosApplicationSnapshot left,
        GuideXosApplicationSnapshot right)
    {
        return left.TryGetRecord(0u,
                out GuideXosApplicationSnapshotRecord leftRecord) &&
            right.TryGetRecord(0u,
                out GuideXosApplicationSnapshotRecord rightRecord) &&
            leftRecord.Identity == rightRecord.Identity &&
            leftRecord.recordVersion == rightRecord.recordVersion &&
            leftRecord.state == rightRecord.state &&
            leftRecord.flags == rightRecord.flags &&
            leftRecord.displayNameLength == rightRecord.displayNameLength &&
            leftRecord.applicationIdLength == rightRecord.applicationIdLength &&
            SameRecord(ref leftRecord, ref rightRecord);
    }

    private static bool SameRecord(ref GuideXosApplicationSnapshotRecord left,
        ref GuideXosApplicationSnapshotRecord right)
    {
        fixed (byte* leftName = left.displayName)
        fixed (byte* rightName = right.displayName)
        fixed (byte* leftId = left.applicationId)
        fixed (byte* rightId = right.applicationId)
        {
            for (int index = 0; index < GxAbi.ApplicationSnapshotDisplayNameBytes; ++index)
                if (leftName[index] != rightName[index]) return false;
            for (int index = 0; index < GxAbi.ApplicationSnapshotApplicationIdBytes; ++index)
                if (leftId[index] != rightId[index]) return false;
        }
        return true;
    }

    private static bool HasActiveManagedIdentity(
        GuideXosApplicationSnapshot snapshot, uint selector)
    {
        ReadOnlySpan<byte> expected = selector switch
        {
            4u => "com.guidexos.apps.managed.notes"u8,
            5u => "com.guidexos.apps.managed.settingscenter"u8,
            6u => "com.guidexos.apps.managed.calculator"u8,
            7u => "com.guidexos.apps.managed.taskmanager"u8,
            8u => "com.guidexos.apps.managed.fileexplorer"u8,
            _ => ReadOnlySpan<byte>.Empty,
        };
        if (expected.IsEmpty) return false;
        uint activeCount = 0u;
        bool found = false;
        for (uint index = 0u; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record)) return false;
            if (record.IsActive) ++activeCount;
            if (record.source == GuideXosApplicationSnapshotSource.ManagedLogicalApplication &&
                ApplicationIdEquals(ref record, expected))
                found = record.IsActive;
        }
        return found && activeCount == 1u;
    }

    private static bool TryGetPreviousManagedIdentity(uint selector,
        out GuideXosApplicationInstanceId identity)
    {
        switch (selector)
        {
            case 4u: identity = s_notesIdentity; return s_hasNotesIdentity;
            case 5u: identity = s_settingsIdentity; return s_hasSettingsIdentity;
            case 6u: identity = s_calculatorIdentity; return s_hasCalculatorIdentity;
            case 7u: identity = s_taskManagerIdentity; return s_hasTaskManagerIdentity;
            case 8u: identity = s_fileExplorerIdentity; return s_hasFileExplorerIdentity;
            default: identity = default; return false;
        }
    }

    private static void SetManagedIdentity(uint selector,
        GuideXosApplicationInstanceId identity)
    {
        switch (selector)
        {
            case 4u: s_notesIdentity = identity; s_hasNotesIdentity = true; break;
            case 5u: s_settingsIdentity = identity; s_hasSettingsIdentity = true; break;
            case 6u: s_calculatorIdentity = identity; s_hasCalculatorIdentity = true; break;
            case 7u: s_taskManagerIdentity = identity; s_hasTaskManagerIdentity = true; break;
            case 8u: s_fileExplorerIdentity = identity; s_hasFileExplorerIdentity = true; break;
        }
    }

    private static bool ApplicationIdEquals(
        ref GuideXosApplicationSnapshotRecord record,
        ReadOnlySpan<byte> expected)
    {
        if (record.applicationIdLength != expected.Length) return false;
        fixed (byte* value = record.applicationId)
        {
            for (int index = 0; index < expected.Length; ++index)
                if (value[index] != expected[index]) return false;
        }
        return true;
    }

    private static nuint FieldOffset(void* baseAddress, void* fieldAddress) =>
        (nuint)fieldAddress - (nuint)baseAddress;

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        if (!passed)
        {
            int bit = count - 1;
            if (bit < 32) s_failedCasesLow |= 1u << bit;
            else if (bit < 64) s_failedCasesHigh |= 1u << (bit - 32);
        }
        return passed;
    }

    private static void LogProductionIdentity(GuideXosHost host,
        uint selector, GuideXosApplicationInstanceId identity,
        uint total, uint activeCount, bool replacement)
    {
        Span<byte> line = stackalloc byte[224];
        int position = 0;
        Append(line, ref position, "C160-SNAPSHOT appId="u8);
        Append(line, ref position, selector switch
        {
            4u => "com.guidexos.apps.managed.notes"u8,
            5u => "com.guidexos.apps.managed.settingscenter"u8,
            7u => "com.guidexos.apps.managed.taskmanager"u8,
            8u => "com.guidexos.apps.managed.fileexplorer"u8,
            _ => "com.guidexos.apps.managed.calculator"u8,
        });
        Append(line, ref position, " source="u8);
        AppendUnsigned(line, ref position, (uint)identity.Source);
        Append(line, ref position, " instance="u8);
        AppendUnsigned64(line, ref position, identity.Value);
        Append(line, ref position, " count="u8);
        AppendUnsigned(line, ref position, total);
        Append(line, ref position, " active="u8);
        AppendUnsigned(line, ref position, activeCount);
        Append(line, ref position, replacement
            // TryLog accepts at most 127 bytes; keep even the longest
            // Calculator lifetime line within that ABI bound.
            ? " prev=gone distinct=1 result=PASS"u8
            : " previous=none result=PASS"u8);
        host.TryLog(line[..position]);
    }

    private static void Append(Span<byte> destination, ref int position,
        ReadOnlySpan<byte> value)
    {
        value.CopyTo(destination[position..]);
        position += value.Length;
    }

    private static void AppendUnsigned(Span<byte> destination,
        ref int position, uint value)
    {
        Span<byte> digits = stackalloc byte[10];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (count > 0) destination[position++] = digits[--count];
    }

    private static void AppendUnsigned64(Span<byte> destination,
        ref int position, ulong value)
    {
        Span<byte> digits = stackalloc byte[20];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (count > 0) destination[position++] = digits[--count];
    }
}
#endif
