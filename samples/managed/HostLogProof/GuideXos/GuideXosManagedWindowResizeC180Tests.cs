#if HOSTLOGPROOF_C174_SHARED_MANAGED_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedWindowResizeC180Tests
{
    public const int CaseCount = 21;
    public const int StressCaseCount = 7;
    public const int StressTransactions = 1000;
    public const int StressAlternatingRequests = 500;
    public const int StressRejectedRequests = 250;
    public const int StressCrossApplicationRequests = 250;
    public const int StressLifecycleCycles = 100;

    private static readonly GuideXosManagedWindowResizeBoundsC174 ProbeBounds =
        new(100, 80, 300, 220);

    public static int FirstFailedCase { get; private set; }
    public static string StressFailureDetail { get; private set; } = string.Empty;
    public static int StressTransactionCount { get; private set; }
    public static int StressAlternatingCount { get; private set; }
    public static int StressRejectedCount { get; private set; }
    public static int StressCrossApplicationCount { get; private set; }
    public static int StressLifecycleCount { get; private set; }
    public static long StressAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }

    public static bool Run(GuideXosHost host)
    {
        _ = host;
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        ManagedSettingsCenter settings = new();
        ManagedFileExplorerC163 explorer = new();
        ManagedNotes notes = new();
        ManagedCalculatorC158 calculator = new();
        ManagedTaskManagerC161 taskManager = new();
        passed &= Case(ref cases, "five-app-explicit-opt-in",
            SupportsResize(settings) && SupportsResize(explorer) &&
            SupportsResize(notes) && SupportsResize(calculator) &&
            SupportsResize(taskManager) &&
            RegisteredResizeApplication(4u) &&
            RegisteredResizeApplication(5u) &&
            RegisteredResizeApplication(6u) &&
            RegisteredResizeApplication(7u) &&
            RegisteredResizeApplication(8u));
        passed &= Case(ref cases, "settings-bounds",
            BoundsMatch(settings, 512, 222, 720, 420));
        passed &= Case(ref cases, "explorer-bounds",
            BoundsMatch(explorer, 600, 360, 960, 526));
        passed &= Case(ref cases, "notes-bounds",
            BoundsMatch(notes, 560, 344, 960, 436));
        passed &= Case(ref cases, "calculator-bounds",
            BoundsMatch(calculator, 284, 262, 512, 420));
        passed &= Case(ref cases, "task-manager-bounds",
            BoundsMatch(taskManager, 800, 344, 960, 452));
        passed &= Case(ref cases, "shared-titlebar",
            GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight == 24);

        GuideXosManagedWindowResizeBoundsC174 malformedBounds = new(
            100, 23, 300, 220);
        passed &= Case(ref cases, "invalid-bounds-configuration",
            !malformedBounds.TryCreateCandidate(100, 80, out _));

        PlainProbe nonOptedIn = new();
        uint minimumAction = Encode(100, 80);
        passed &= Case(ref cases, "non-opted-in-resize-rejected",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                nonOptedIn, null, minimumAction) == GuideXosResult.InvalidAction &&
            nonOptedIn.ResizeCalls == 0);
        ResizeProbe ordinaryAction = new();
        passed &= Case(ref cases, "ordinary-action-keeps-existing-route",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                ordinaryAction, null, 0x1234u) ==
                GuideXosResult.CapabilityUnavailable &&
            ordinaryAction.OrdinaryActionCalls == 1);

        ResizeProbe accepted = new();
        GuideXosResult minimumResult =
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                accepted, null, minimumAction);
        passed &= Case(ref cases, "minimum-request-routed",
            minimumResult == GuideXosResult.Success &&
            accepted.ResizeCalls == 1 && accepted.Width == 100 &&
            accepted.Height == 80);
        passed &= Case(ref cases, "candidate-has-client-geometry",
            accepted.LastClientWidth == 100 &&
            accepted.LastClientHeight == 56);

        ResizeProbe noOp = new(100, 80);
        passed &= Case(ref cases, "no-op-does-not-call-app-layout",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                noOp, null, minimumAction) == GuideXosResult.Success &&
            noOp.ResizeCalls == 0);

        ResizeProbe invalid = new();
        passed &= Case(ref cases, "one-pixel-outside-rejected-before-callback",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                invalid, null, Encode(99, 80)) ==
                GuideXosResult.InvalidArgument && invalid.ResizeCalls == 0);
        ResizeProbe badPrior = new(99, 80);
        passed &= Case(ref cases, "invalid-current-geometry-rejected",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                badPrior, null, Encode(120, 90)) ==
                GuideXosResult.InvalidArgument && badPrior.ResizeCalls == 0);

        ResizeProbe layoutFailure = new() { RejectLayout = true };
        passed &= Case(ref cases, "layout-rejection-rolls-back",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                layoutFailure, null, Encode(300, 220)) ==
                GuideXosResult.InvalidArgument &&
            layoutFailure.Width == 160 && layoutFailure.Height == 120 &&
            layoutFailure.RollbackCalls == 1);
        ResizeProbe renderFailure = new()
        {
            RenderResult = GuideXosResult.CapabilityUnavailable,
        };
        passed &= Case(ref cases, "render-failure-result-and-rollback",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                renderFailure, null, Encode(300, 220)) ==
                GuideXosResult.CapabilityUnavailable &&
            renderFailure.Width == 160 && renderFailure.Height == 120 &&
            renderFailure.RollbackCalls == 1);
        ResizeProbe stateFailure = new() { RejectState = true };
        passed &= Case(ref cases, "state-rejection-rolls-back",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                stateFailure, null, Encode(300, 220)) ==
                GuideXosResult.InvalidArgument &&
            stateFailure.Width == 160 && stateFailure.Height == 120 &&
            stateFailure.State == 7 && stateFailure.RollbackCalls == 1);

        ResizeProbe maximum = new();
        passed &= Case(ref cases, "maximum-request-accepted",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                maximum, null, Encode(300, 220)) == GuideXosResult.Success &&
            maximum.Width == 300 && maximum.Height == 220);
        passed &= Case(ref cases, "identity-and-app-state-retained",
            maximum.WindowIdentity == 0xC180u && maximum.State == 7);
        passed &= Case(ref cases, "bounds-are-independent-per-instance",
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                maximum, null, Encode(301, 220)) ==
                    GuideXosResult.InvalidArgument &&
            maximum.ResizeCalls == 1);

        return passed && cases == CaseCount;
    }

    public static bool RunStress(GuideXosHost host)
    {
        _ = host;
        FirstFailedCase = 0;
        StressFailureDetail = string.Empty;
        StressTransactionCount = 0;
        StressAlternatingCount = 0;
        StressRejectedCount = 0;
        StressCrossApplicationCount = 0;
        StressLifecycleCount = 0;
        StressAllocationBytes = -1;
        StressDriftCount = -1;
        int cases = 0;
        bool passed = true;

        ResizeProbe transactionProbe = new();
        _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(
            transactionProbe, null, Encode(200, 140));
        long allocationBefore = GC.GetAllocatedBytesForCurrentThread();
        bool transactionRun = true;
        for (int index = 0; index < StressTransactions; index++)
        {
            int width = 100 + index % 201;
            int height = 80 + index % 141;
            GuideXosResult result =
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    transactionProbe, null, Encode(width, height));
            transactionRun &= result == GuideXosResult.Success;
        }
        StressAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocationBefore;
        StressTransactionCount = StressTransactions;
        passed &= Case(ref cases, "1000-shared-transactions",
            transactionRun && transactionProbe.ResizeCalls > 0);

        ResizeProbe alternating = new();
        bool alternatingRun = true;
        for (int index = 0; index < StressAlternatingRequests; index++)
        {
            uint action = index % 2 == 0
                ? Encode(100, 80) : Encode(300, 220);
            alternatingRun &=
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    alternating, null, action) == GuideXosResult.Success;
        }
        StressAlternatingCount = StressAlternatingRequests;
        passed &= Case(ref cases, "500-alternating-valid-requests",
            alternatingRun && alternating.Width == 300 &&
            alternating.Height == 220);

        ResizeProbe rejected = new();
        bool rejectsOutside = true;
        for (int index = 0; index < StressRejectedRequests; index++)
        {
            rejectsOutside &=
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    rejected, null, Encode(99, 80)) ==
                GuideXosResult.InvalidArgument;
        }
        StressRejectedCount = StressRejectedRequests;
        passed &= Case(ref cases, "250-rejected-geometry-requests",
            rejectsOutside && rejected.ResizeCalls == 0);

        ResizeProbe[] applications =
        {
            new(120, 90, 11), new(130, 100, 22), new(140, 110, 33),
            new(150, 120, 44), new(160, 130, 55),
        };
        bool isolated = true;
        int crossRequests = 0;
        for (int index = 0; index < StressCrossApplicationRequests; index++)
        {
            int appIndex = index % applications.Length;
            ResizeProbe app = applications[appIndex];
            int priorState = app.State;
            uint otherIdentity = applications[(appIndex + 1) %
                applications.Length].WindowIdentity;
            int width = app.Width == 100 ? 300 : 100;
            int height = app.Height == 80 ? 220 : 80;
            GuideXosResult result =
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    app, null, Encode(width, height));
            isolated &= result == GuideXosResult.Success &&
                app.State == priorState && app.WindowIdentity != otherIdentity;
            crossRequests++;
        }
        StressCrossApplicationCount = crossRequests;
        passed &= Case(ref cases, "250-cross-application-state-combinations",
            isolated);

        bool lifecycle = true;
        for (int index = 0; index < StressLifecycleCycles; index++)
        {
            ResizeProbe app = new(100, 80, (uint)(index + 1));
            lifecycle &=
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    app, null, Encode(300, 220)) == GuideXosResult.Success &&
                app.Width == 300 && app.Height == 220 &&
                app.State == 7;
            app.OnTearingDown();
            lifecycle &= !app.IsResident;
        }
        StressLifecycleCount = StressLifecycleCycles;
        passed &= Case(ref cases, "100-lifecycle-cycles", lifecycle);

        passed &= Case(ref cases, "zero-dispatch-hot-path-allocations",
            StressAllocationBytes == 0);
        ResizeProbe driftProbe = new();
        int originalWidth = driftProbe.Width;
        int originalHeight = driftProbe.Height;
        bool roundTrips = true;
        for (int index = 0; index < 100; index++)
        {
            roundTrips &=
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    driftProbe, null, Encode(300, 220)) ==
                GuideXosResult.Success &&
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    driftProbe, null, Encode(originalWidth, originalHeight)) ==
                GuideXosResult.Success;
        }
        StressDriftCount = driftProbe.Width == originalWidth &&
            driftProbe.Height == originalHeight ? 0 : 1;
        passed &= Case(ref cases, "zero-cumulative-geometry-drift",
            roundTrips && StressDriftCount == 0);

        if (!passed && FirstFailedCase == 0)
            StressFailureDetail = "stress-workload";
        return passed && cases == StressCaseCount;
    }

    private static bool BoundsMatch(GuideXosApplication application,
        int minimumWidth, int minimumHeight, int maximumWidth,
        int maximumHeight)
    {
        return application.TryGetManagedResizeGeometryC180(out var bounds,
                out _, out _) &&
            bounds.MinimumOuterWidth == minimumWidth &&
            bounds.MinimumOuterHeight == minimumHeight &&
            bounds.MaximumOuterWidth == maximumWidth &&
            bounds.MaximumOuterHeight == maximumHeight;
    }

    private static bool SupportsResize(GuideXosApplication application) =>
        application.TryGetManagedResizeGeometryC180(out _, out _, out _);

    private static bool RegisteredResizeApplication(uint selector)
    {
        if (!GuideXosApplicationRegistry.TryFind(selector,
                out GuideXosApplicationDescriptor descriptor))
            return false;
#if HOSTLOGPROOF_MANAGED_APP_RETURN
        return descriptor.Factory != null &&
            SupportsResize(descriptor.Factory());
#else
        return SupportsResize(descriptor.Application);
#endif
    }

    private static uint Encode(int width, int height)
    {
        if (!GuideXosManagedWindowGeometryC172.TryEncodeAction(width, height,
                out uint actionId))
            throw new InvalidOperationException("Test geometry did not encode.");
        return actionId;
    }

    private static bool Case(ref int cases, string name, bool condition)
    {
        cases++;
        if (!condition && FirstFailedCase == 0)
        {
            FirstFailedCase = cases;
            StressFailureDetail = name;
        }
        return condition;
    }

    private sealed class ResizeProbe : GuideXosApplication
    {
        private readonly uint _identity;
        private int _width;
        private int _height;

        public ResizeProbe(int width = 160, int height = 120,
            uint identity = 0xC180u)
        {
            _width = width;
            _height = height;
            _identity = identity;
        }

        public bool RejectLayout { get; init; }
        public bool RejectState { get; init; }
        public GuideXosResult RenderResult { get; init; } =
            GuideXosResult.Success;
        public int ResizeCalls { get; private set; }
        public int OrdinaryActionCalls { get; private set; }
        public int RollbackCalls { get; private set; }
        public int LastClientWidth { get; private set; }
        public int LastClientHeight { get; private set; }
        public int State { get; private set; } = 7;
        public int Width => _width;
        public int Height => _height;
        public uint WindowIdentity => _identity;
        public bool IsResident { get; private set; } = true;

        internal override bool TryGetManagedResizeGeometryC180(
            out GuideXosManagedWindowResizeBoundsC174 bounds,
            out int outerWidth, out int outerHeight)
        {
            bounds = ProbeBounds;
            outerWidth = _width;
            outerHeight = _height;
            return true;
        }

        public override GuideXosResult Launch(GuideXosHost host)
        {
            _ = host;
            IsResident = true;
            return GuideXosResult.Success;
        }

        public override GuideXosResult HandleAction(GuideXosHost host,
            uint actionId)
        {
            _ = host;
            _ = actionId;
            OrdinaryActionCalls++;
            return GuideXosResult.CapabilityUnavailable;
        }

        public override void OnTearingDown() => IsResident = false;

        internal override GuideXosResult HandleManagedResizeC180(
            GuideXosHost host,
            ref GuideXosManagedWindowResizeTransactionC174 transaction)
        {
            _ = host;
            ResizeCalls++;
            LastClientWidth = transaction.Candidate.ClientWidth;
            LastClientHeight = transaction.Candidate.ClientHeight;
            int priorWidth = _width;
            int priorHeight = _height;
            int priorState = State;
            if (RejectLayout)
            {
                _ = transaction.Complete(false, GuideXosResult.Success, false);
                return Rollback(in transaction, priorWidth, priorHeight,
                    priorState);
            }

            _width = transaction.Candidate.OuterWidth;
            _height = transaction.Candidate.OuterHeight;
            if (RejectState) State++;
            bool accepted = transaction.Complete(true, RenderResult,
                !RejectState && State == priorState);
            if (!accepted)
                return Rollback(in transaction, priorWidth, priorHeight,
                    priorState);
            return transaction.Result;
        }

        private GuideXosResult Rollback(
            in GuideXosManagedWindowResizeTransactionC174 transaction,
            int priorWidth, int priorHeight, int priorState)
        {
            if (transaction.RequiresRollback)
            {
                _width = priorWidth;
                _height = priorHeight;
                State = priorState;
                RollbackCalls++;
            }
            return transaction.Result;
        }
    }

    private sealed class PlainProbe : GuideXosApplication
    {
        public int ResizeCalls { get; private set; }

        public override GuideXosResult Launch(GuideXosHost host)
        {
            _ = host;
            return GuideXosResult.Success;
        }

        public override GuideXosResult HandleAction(GuideXosHost host,
            uint actionId)
        {
            _ = host;
            _ = actionId;
            ResizeCalls++;
            return GuideXosResult.Success;
        }
    }
}
#endif
