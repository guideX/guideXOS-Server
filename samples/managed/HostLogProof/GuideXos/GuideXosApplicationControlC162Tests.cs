#if HOSTLOGPROOF_C162_MANAGED_TASK_MANAGER_CLOSE
using System.Runtime.InteropServices;

namespace HostLogProof;

internal static unsafe class GuideXosApplicationControlC162Tests
{
    private static uint s_seenSource;
    private static ulong s_seenIdentity;
    private static int s_stubResult;
    private static int s_callCount;

    [UnmanagedCallersOnly]
    private static int CloseStub(
        NativeGxAppContext* context, uint source, ulong instanceId)
    {
        if (context == null) return -2;
        s_seenSource = source;
        s_seenIdentity = instanceId;
        ++s_callCount;
        return s_stubResult;
    }

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        NativeHostCallTable tableLayout = default;
        NativeHostCallTable* tablePointer = &tableLayout;
        bool layout = sizeof(NativeHostCallTable) == GxAbi.HostCallTableSize &&
            FieldOffset(tablePointer, &tablePointer->applicationSnapshot) ==
                GxAbi.ApplicationSnapshotOffset &&
            FieldOffset(tablePointer, &tablePointer->closeApplication) ==
                GxAbi.ApplicationCloseOffset &&
            GxAbi.ApplicationSnapshotOffset + sizeof(ulong) ==
                GxAbi.HostCallTableV2Size &&
            GxAbi.ApplicationCloseOffset + sizeof(ulong) ==
                GxAbi.HostCallTableSize;
        passed &= Case(ref cases, layout);

        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        context.apiVersion = GxAbi.ApiVersion;
        GuideXosApplicationInstanceId identity = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance,
            0xFEDCBA9876543210UL);

        NativeHostCallTable v2 = MakeTable(2u,
            GxAbi.HostCallTableV2Size,
            GxAbi.CapabilityApplicationClose, &CloseStub);
        context.host = &v2;
        s_callCount = 0;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v2, identity) ==
                GuideXosApplicationCloseResult.NotSupported && s_callCount == 0);

        NativeHostCallTable shortV3 = MakeTable(3u,
            GxAbi.HostCallTableSize - 1u,
            GxAbi.CapabilityApplicationClose, &CloseStub);
        context.host = &shortV3;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &shortV3, identity) ==
                GuideXosApplicationCloseResult.NotSupported && s_callCount == 0);

        NativeHostCallTable v1 = MakeTable(1u,
            GxAbi.HostCallTableSize,
            GxAbi.CapabilityApplicationClose, &CloseStub);
        context.host = &v1;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v1, identity) ==
                GuideXosApplicationCloseResult.NotSupported && s_callCount == 0);

        NativeHostCallTable noCapability = MakeTable(3u,
            GxAbi.HostCallTableSize, 0u, &CloseStub);
        context.host = &noCapability;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &noCapability, identity) ==
                GuideXosApplicationCloseResult.CapabilityUnavailable &&
                s_callCount == 0);

        NativeHostCallTable noCallback = MakeTable(3u,
            GxAbi.HostCallTableSize, GxAbi.CapabilityApplicationClose, null);
        context.host = &noCallback;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &noCallback, identity) ==
                GuideXosApplicationCloseResult.NotSupported && s_callCount == 0);

        NativeHostCallTable v3 = MakeTable(3u,
            GxAbi.HostCallTableSize, GxAbi.CapabilityApplicationClose,
            &CloseStub);
        context.host = &v3;
        s_stubResult = 0;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.Success &&
            s_callCount == 1 &&
            s_seenSource == (uint)identity.Source &&
            s_seenIdentity == identity.Value);

        s_stubResult = -10;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.NotFound);
        s_stubResult = -11;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.Protected);
        s_stubResult = -12;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.CloseFailed);
        s_stubResult = -13;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.StaleIdentity);
        s_stubResult = -14;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.Pending);
        s_stubResult = 77;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &v3, identity) == GuideXosApplicationCloseResult.NativeFailure);

        int callsBeforeInvalid = s_callCount;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(&context, &v3,
                new GuideXosApplicationInstanceId(identity.Source, 0u)) ==
                GuideXosApplicationCloseResult.InvalidArgument &&
            GuideXosApplicationControl.TryCloseApplication(&context, &v3,
                new GuideXosApplicationInstanceId(
                    (GuideXosApplicationSnapshotSource)99u, 55u)) ==
                GuideXosApplicationCloseResult.InvalidArgument &&
            s_callCount == callsBeforeInvalid);

        NativeHostCallTable unknown = MakeTable(99u,
            GxAbi.HostCallTableSize, GxAbi.CapabilityApplicationClose,
            &CloseStub);
        context.host = &unknown;
        s_stubResult = 0;
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(
                &context, &unknown, identity) == GuideXosApplicationCloseResult.Success);
        passed &= Case(ref cases,
            GuideXosApplicationControl.TryCloseApplication(null, &v3, identity) ==
                GuideXosApplicationCloseResult.InvalidArgument &&
            GuideXosApplicationControl.TryCloseApplication(&context, null, identity) ==
                GuideXosApplicationCloseResult.InvalidArgument);
        passed &= Case(ref cases, TestCapturedIdentityAba());
        passed &= Case(ref cases, TestCancelClearsCapturedIdentity());
        _ = host;
        host?.TryLog(passed
            ? "C162-CLOSE-ABI cases=18 v2=NotSupported identity=source+u64 result=PASS"u8
            : "C162-CLOSE-ABI result=FAIL"u8);
        return passed && cases == 18;
    }

    public static bool RunNativeBoundary(GuideXosHost host)
    {
        int cases = 0;
        GuideXosApplicationSnapshot snapshot = default;
        GuideXosApplicationSnapshotResult snapshotResult = host == null
            ? GuideXosApplicationSnapshotResult.InvalidArgument
            : host.TryGetApplicationSnapshot(out snapshot);
        bool passed = host != null && (snapshotResult is
            GuideXosApplicationSnapshotResult.Success or
            GuideXosApplicationSnapshotResult.Truncated);
        bool foundSelf = false;
        bool foundShell = false;
        GuideXosApplicationInstanceId self = default;
        GuideXosApplicationInstanceId shell = default;
        if (passed)
        {
            for (uint index = 0u; index < snapshot.Count; ++index)
            {
                if (!snapshot.TryGetRecord(index,
                        out GuideXosApplicationSnapshotRecord record)) continue;
                if (record.source ==
                        GuideXosApplicationSnapshotSource.ManagedLogicalApplication &&
                    record.GetApplicationId() ==
                        "com.guidexos.apps.managed.taskmanager")
                {
                    foundSelf = true;
                    self = record.Identity;
                }
                else if (record.source ==
                        GuideXosApplicationSnapshotSource.ShellSurface)
                {
                    foundShell = true;
                    shell = record.Identity;
                }
            }
        }
        passed &= Case(ref cases, foundSelf && self.Value != 0u &&
            host.TryCloseApplication(self) ==
                GuideXosApplicationCloseResult.Protected);
        passed &= Case(ref cases, foundShell && shell.Value != 0u &&
            host.TryCloseApplication(shell) ==
                GuideXosApplicationCloseResult.Protected);
        passed &= Case(ref cases, host.TryCloseApplication(
            new GuideXosApplicationInstanceId(
                GuideXosApplicationSnapshotSource.ManagedLogicalApplication,
                ulong.MaxValue)) == GuideXosApplicationCloseResult.NotFound);
        host.TryLog(passed
            ? "C162-CLOSE-NATIVE-BOUNDARY cases=3 self=Protected shell=Protected unknown=NotFound result=PASS"u8
            : "C162-CLOSE-NATIVE-BOUNDARY result=FAIL"u8);
        return passed && cases == 3;
    }

    private static NativeHostCallTable MakeTable(uint version, uint size,
        ulong capabilities,
        delegate* unmanaged<NativeGxAppContext*, uint, ulong, int> callback)
    {
        NativeHostCallTable table = default;
        table.version = version;
        table.size = size;
        table.capabilities = capabilities;
        table.closeApplication = callback;
        return table;
    }

    private static nuint FieldOffset(void* baseAddress, void* fieldAddress) =>
        (nuint)fieldAddress - (nuint)baseAddress;

    private static bool TestCapturedIdentityAba()
    {
        GuideXosPendingCloseTargetC162 pending = new();
        GuideXosApplicationInstanceId oldIdentity = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance, 200UL);
        GuideXosApplicationInstanceId replacement = new(
            GuideXosApplicationSnapshotSource.AppManagerInstance, 201UL);
        if (!pending.Capture(oldIdentity, "Application A")) return false;
        s_stubResult = -10;
        s_callCount = 0;
        NativeGxAppContext context = default;
        context.size = (uint)sizeof(NativeGxAppContext);
        NativeHostCallTable table = MakeTable(3u, GxAbi.HostCallTableSize,
            GxAbi.CapabilityApplicationClose, &CloseStub);
        context.host = &table;
        bool retained = pending.TryGet(
            out GuideXosApplicationInstanceId captured, out string displayName);
        GuideXosApplicationCloseResult result =
            GuideXosApplicationControl.TryCloseApplication(
                &context, &table, captured);
        bool stillCaptured = pending.TryGet(
            out GuideXosApplicationInstanceId retainedIdentity, out _);
        pending.Clear();
        return retained && stillCaptured && retainedIdentity == oldIdentity &&
            displayName == "Application A" &&
            result == GuideXosApplicationCloseResult.NotFound &&
            s_callCount == 1 && s_seenSource == (uint)oldIdentity.Source &&
            s_seenIdentity == oldIdentity.Value &&
            captured != replacement && !pending.HasTarget;
    }

    private static bool TestCancelClearsCapturedIdentity()
    {
        GuideXosPendingCloseTargetC162 pending = new();
        GuideXosApplicationInstanceId identity = new(
            GuideXosApplicationSnapshotSource.ManagedLogicalApplication, 88UL);
        int callsBeforeCancel = s_callCount;
        bool captured = pending.Capture(identity, "Managed Calculator") &&
            pending.HasTarget && pending.Identity == identity &&
            pending.DisplayName == "Managed Calculator";
        pending.Clear();
        return captured && !pending.HasTarget &&
            s_callCount == callsBeforeCancel;
    }

    private static bool Case(ref int count, bool result)
    {
        ++count;
        return result;
    }
}
#endif
