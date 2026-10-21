#if HOSTLOGPROOF_C185_BOUNDARY
using System;
using GuideXos.C185.CounterApp;
using GuideXos.ManagedUi;

namespace HostLogProof;

public static unsafe partial class GuideXosManagedHostServicesC185Tests
{
    public const int FocusedCaseCountC186 = 9;
    public const int StressCheckCountC186 = 6;
    public const int StressServiceOperationCountC186 = 1000;
    public const int StressValidResizeCountC186 = 500;
    public const int StressRejectedGeometryCountC186 = 250;
    public const int StressFailureRecoveryCountC186 = 250;
    public const int StressStaleCallbackCountC186 = 250;
    public const int StressLifecycleCountC186 = 100;

    private static int s_c186FirstFailedCase;
    private static int s_c186Cases;
    private static string s_c186FailureDetail = string.Empty;

    public static int C186FirstFailedCase => s_c186FirstFailedCase;
    public static string C186FailureDetail => s_c186FailureDetail;
    public static int C186ServiceOperations { get; private set; }
    public static int C186ValidResizes { get; private set; }
    public static int C186RejectedGeometry { get; private set; }
    public static int C186FailureRecovery { get; private set; }
    public static int C186StaleCallbacks { get; private set; }
    public static int C186Lifecycles { get; private set; }
    public static long C186ResizeAllocationBytes { get; private set; }
    public static int C186GeometryDrift { get; private set; }

    public static bool RunC186()
    {
        s_c186FirstFailedCase = 0;
        s_c186Cases = 0;
        s_c186FailureDetail = string.Empty;
        C186ServiceOperations = 0;
        C186ValidResizes = 0;
        C186RejectedGeometry = 0;
        C186FailureRecovery = 0;
        C186StaleCallbacks = 0;
        C186Lifecycles = 0;
        C186ResizeAllocationBytes = -1;
        C186GeometryDrift = -1;

        bool passed = true;
        passed &= C186Case(RunIdentityAndOwnershipIsolation());
        passed &= C186Case(RunDirectCloseInvalidatesCallbacks());
        passed &= C186Case(RunFailedCloseRetainsRetryableOwnership());
        passed &= C186Case(RunWindowCreationFailureLeavesNoOwnership());
        passed &= C186Case(RunReturnedInitializationFailureReleasesPartialWindow());
        passed &= C186Case(RunThrownInitializationFailureReleasesPartialWindow());
        passed &= C186Case(RunResizeExceptionRollsBackGeometry());
        passed &= C186Case(RunCapacityReuseAndStaleGeneration());
        passed &= C186Case(RunActivationIsExplicitlyUnsupported());
        passed &= RunStress();
        passed &= s_c186Cases == FocusedCaseCountC186 + StressCheckCountC186;
        return passed && s_c186FirstFailedCase == 0;
    }

    private static bool RunIdentityAndOwnershipIsolation()
    {
        ResetFakeHost();
        uint applicationTypeA = CounterApplicationFactory.StableApplicationId;
        uint applicationTypeB = applicationTypeA + 1u;
        const uint generationA = 101u;
        const uint generationB = 202u;

        NativeHostCallTable tableA = CreateCallTable();
        NativeHostCallTable tableB = CreateCallTable();
        NativeGxAppContext contextA = CreateContext(&tableA, generationA, 0u);
        NativeGxAppContext contextB = CreateContext(&tableB, generationB, 0u);
        if (!GuideXosHost.TryCreate(&contextA, applicationTypeA,
                out GuideXosHost hostA, out _))
            return false;

        IsolatedApplication appA = new();
        IsolatedApplication appB = new();
        GuideXosC185ApplicationBridge bridgeA = new(appA);
        GuideXosC185ApplicationBridge bridgeB = new(appB);
        bridgeA.SetIdentity(applicationTypeA, generationA);
        bridgeB.SetIdentity(applicationTypeB, generationB);
        s_enforceFakeWindowGeneration = true;
        bool startedA = bridgeA.Launch(hostA) == GuideXosResult.Success;
        if (!GuideXosHost.TryCreate(&contextB, applicationTypeB,
                out GuideXosHost hostB, out _))
            return false;
        bool startedB = bridgeB.Launch(hostB) == GuideXosResult.Success;
        GuideXosC185WindowAdapter windowA = bridgeA.ServicesForTests?.Window;
        GuideXosC185WindowAdapter windowB = bridgeB.ServicesForTests?.Window;
        ManagedUiWindowIdentity identityA = windowA?.Identity ?? default;
        ManagedUiWindowIdentity identityB = windowB?.Identity ?? default;

        bool identitiesDistinct = startedA && startedB && windowA != null &&
            windowB != null && identityA.ApplicationTypeId == applicationTypeA &&
            identityA.Generation == generationA && identityA.Ordinal == 1u &&
            identityB.ApplicationTypeId == applicationTypeB &&
            identityB.Generation == generationB && identityB.Ordinal == 1u &&
            identityA.ApplicationTypeId != identityB.ApplicationTypeId &&
            identityA.Generation != identityB.Generation &&
            s_fakeWindowHandles[0] != s_fakeWindowHandles[1];

        ManagedUiResult crossOwnerDraw = windowA == null
            ? ManagedUiResult.InvalidState
            : windowA.Surface.TryFillRect(0, 0, 4, 4, 0u);
        bool crossOwnerRejected = crossOwnerDraw == ManagedUiResult.DrawingFailed &&
            appA.InputCount == 0 && appB.InputCount == 0;

        GuideXosHost.TryCreate(&contextA, applicationTypeA, out hostA, out _);
        ManagedUiResult drawA = windowA?.Surface.TryFillRect(0, 0, 4, 4, 0u) ??
            ManagedUiResult.InvalidState;
        GuideXosHost.TryCreate(&contextB, applicationTypeB, out hostB, out _);
        ManagedUiResult drawB = windowB?.Surface.TryFillRect(0, 0, 4, 4, 0u) ??
            ManagedUiResult.InvalidState;
        bool independentDrawing = drawA == ManagedUiResult.Success &&
            drawB == ManagedUiResult.Success && appA.InputCount == 0 &&
            appB.InputCount == 0;

        bridgeB.OnTearingDown();
        GuideXosHost.TryCreate(&contextA, applicationTypeA, out hostA, out _);
        bool otherInstanceSurvives = windowA?.Surface.TryFillRect(0, 0, 4, 4, 0u) ==
            ManagedUiResult.Success && appA.TeardownCount == 0 &&
            appB.TeardownCount == 1;
        bridgeA.OnTearingDown();
        bool allReleased = bridgeA.StateForTests == GuideXosC185LifecycleState.Closed &&
            bridgeB.StateForTests == GuideXosC185LifecycleState.Closed &&
            ActiveFakeWindowCount() == 0;
        return identitiesDistinct && crossOwnerRejected && independentDrawing &&
            otherInstanceSurvives && allReleased;
    }

    private static bool RunDirectCloseInvalidatesCallbacks()
    {
        ResetFakeHost();
        const uint applicationType = 0xC1861004u;
        const uint generation = 304u;
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation, 0u);
        if (!GuideXosHost.TryCreate(&context, applicationType,
                out GuideXosHost host, out _))
            return false;
        CloseOnInputApplication app = new();
        GuideXosC185ApplicationBridge bridge = new(app);
        bridge.SetIdentity(applicationType, generation);
        if (bridge.Launch(host) != GuideXosResult.Success) return false;
        GuideXosC185WindowAdapter window = bridge.ServicesForTests?.Window;
        if (window == null) return false;
        int closeAttempts = s_closeRequests;
        IManagedUiWindow cachedWindow = window;
        IManagedUiSurface cachedSurface = window.Surface;
        GuideXosInputEvent input = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary, 10, 10);
        bool closed = bridge.HandleInput(host, input) == GuideXosResult.Success &&
            app.CloseResult == ManagedUiResult.Success;
        bool inert = cachedSurface.TryFillRect(0, 0, 4, 4, 0u) ==
                ManagedUiResult.InvalidState && cachedWindow.RequestRedraw() ==
                ManagedUiResult.InvalidState && window.NativeSurface == null &&
            bridge.StateForTests == GuideXosC185LifecycleState.Closing;
        bool staleInput = bridge.HandleInput(host, input) ==
            GuideXosResult.InvalidArgument;
        bool encoded = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            400, 240, out uint actionId);
        bool staleResize = !encoded ||
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                bridge, host, actionId) != GuideXosResult.Success;
        bool repeatedClose = cachedWindow.TryClose() == ManagedUiResult.Success &&
            s_closeRequests == closeAttempts + 1;
        bridge.OnTearingDown();
        bool teardown = app.TeardownCount == 1 &&
            bridge.StateForTests == GuideXosC185LifecycleState.Closed &&
            s_closeRequests == closeAttempts + 1;
        return closed && inert && staleInput && staleResize && repeatedClose && teardown;
    }

    private static bool RunFailedCloseRetainsRetryableOwnership()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        GuideXosC185ApplicationBridge bridge = runtime.CurrentBridgeForTests;
        GuideXosC185WindowAdapter window = bridge?.ServicesForTests?.Window;
        if (window == null) return false;
        uint generation = window.Identity.Generation;
        s_failNextClose = true;
        ManagedUiResult failedClose = TryCloseAndDrawInHostScope(
            runtime, generation, out ManagedUiResult drawAfterFailedClose);
        bool remainsUsable = failedClose != ManagedUiResult.Success &&
            bridge.StateForTests == GuideXosC185LifecycleState.Active &&
            window.NativeSurface != null &&
            drawAfterFailedClose == ManagedUiResult.Success;
        bool teardownRetries = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success && s_closeRequests == 2 &&
            s_successfulCloseRequests == 1 && ActiveFakeWindowCount() == 0;
        return remainsUsable && teardownRetries;
    }

    private static bool RunWindowCreationFailureLeavesNoOwnership()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        s_failNextWindow = true;
        GuideXosResult failed = Invoke(runtime, 0u, 0u);
        bool failureClean = failed == GuideXosResult.SurfaceCreationFailed &&
            runtime.CurrentBridgeForTests == null && s_closeRequests == 0 &&
            ActiveFakeWindowCount() == 0;
        GuideXosResult recovered = Invoke(runtime, 0u, 0u);
        GuideXosC185WindowAdapter window = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
        bool recovery = recovered == GuideXosResult.Success && window != null &&
            window.Identity.Generation > 1u && Invoke(runtime,
                window.Identity.Generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success;
        return failureClean && recovery;
    }

    private static bool RunReturnedInitializationFailureReleasesPartialWindow()
    {
        return RunPartialInitializationFailure(throws: false);
    }

    private static bool RunThrownInitializationFailureReleasesPartialWindow()
    {
        return RunPartialInitializationFailure(throws: true);
    }

    private static bool RunPartialInitializationFailure(bool throws)
    {
        ResetFakeHost();
        const uint applicationType = 0xC1861001u;
        const uint generation = 301u;
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation, 0u);
        if (!GuideXosHost.TryCreate(&context, applicationType,
                out GuideXosHost host, out _))
            return false;

        FailingInitializeApplication app = new(throws);
        GuideXosC185ApplicationBridge bridge = new(app);
        bridge.SetIdentity(applicationType, generation);
        GuideXosResult result = bridge.Launch(host);
        bool released = result != GuideXosResult.Success &&
            bridge.StateForTests == GuideXosC185LifecycleState.Failed &&
            bridge.ServicesForTests == null && app.TeardownCount == 1 &&
            s_closeRequests == 1 && s_successfulCloseRequests == 1 &&
            app.SavedWindow?.Surface.TryFillRect(0, 0, 4, 4, 0u) ==
                ManagedUiResult.InvalidState;
        bridge.OnTearingDown();
        return released && app.TeardownCount == 1 && s_closeRequests == 1 &&
            ActiveFakeWindowCount() == 0;
    }

    private static bool RunResizeExceptionRollsBackGeometry()
    {
        ResetFakeHost();
        const uint applicationType = 0xC1861002u;
        const uint generation = 302u;
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation, 0u);
        if (!GuideXosHost.TryCreate(&context, applicationType,
                out GuideXosHost host, out _))
            return false;

        ThrowingResizeApplication app = new();
        GuideXosC185ApplicationBridge bridge = new(app);
        bridge.SetIdentity(applicationType, generation);
        if (bridge.Launch(host) != GuideXosResult.Success || app.Window == null)
            return false;
        bool encoded = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            120, 90, out uint actionId);
        GuideXosResult resize = encoded
            ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                bridge, host, actionId)
            : GuideXosResult.InvalidArgument;
        bool rolledBack = resize == GuideXosResult.InvalidArgument &&
            app.Window.OuterWidth == 100 && app.Window.OuterHeight == 80 &&
            app.CompleteResizeCalls == 1 && !app.LastResizeAccepted;
        bridge.OnTearingDown();
        return rolledBack && s_closeRequests == 1 &&
            s_successfulCloseRequests == 1;
    }

    private static bool RunCapacityReuseAndStaleGeneration()
    {
        bool poolPassed = RunLifetimePoolBoundaryChecks();
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        GuideXosC185WindowAdapter oldWindow = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
        if (oldWindow == null) return false;
        uint oldGeneration = oldWindow.Identity.Generation;
        ulong oldHandle = oldWindow.NativeSurface?.Handle ?? 0u;
        if (Invoke(runtime, oldGeneration,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) !=
            GuideXosResult.Success || Invoke(runtime, 0u, 0u) != GuideXosResult.Success)
            return false;
        GuideXosC185WindowAdapter newWindow = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
        bool reusedFresh = newWindow != null &&
            newWindow.Identity.Generation > oldGeneration &&
            newWindow.NativeSurface?.Handle != oldHandle &&
            oldWindow.Surface.TryFillRect(0, 0, 4, 4, 0u) ==
                ManagedUiResult.InvalidState &&
            Invoke(runtime, oldGeneration, PointerDown(250, 140)) ==
                GuideXosResult.InvalidArgument;
        bool closed = newWindow != null && Invoke(runtime,
            newWindow.Identity.Generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success;
        return poolPassed && reusedFresh && closed;
    }

    private static bool RunActivationIsExplicitlyUnsupported()
    {
        ResetFakeHost();
        const uint applicationType = 0xC1861003u;
        const uint generation = 303u;
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation, 0u);
        if (!GuideXosHost.TryCreate(&context, applicationType,
                out GuideXosHost host, out _))
            return false;
        IsolatedApplication app = new();
        GuideXosC185ApplicationBridge bridge = new(app);
        bridge.SetIdentity(applicationType, generation);
        if (bridge.Launch(host) != GuideXosResult.Success) return false;
        GuideXosC185WindowAdapter window = bridge.ServicesForTests.Window;
        bool unsupported = window.RequestActivation() == ManagedUiResult.Unsupported &&
            bridge.StateForTests == GuideXosC185LifecycleState.Active &&
            s_closeRequests == 0;
        bridge.OnTearingDown();
        return unsupported && s_closeRequests == 1;
    }

    private static int ActiveFakeWindowCount()
    {
        int count = 0;
        for (int index = 0; index < s_fakeWindowHandles.Length; index++)
            if (s_fakeWindowHandles[index] != 0u) count++;
        return count;
    }

    private static ManagedUiResult TryCloseAndDrawInHostScope(
        GuideXosC185Runtime runtime, uint generation,
        out ManagedUiResult drawResult)
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation,
            GxAbi.LaunchFlagInput);
        if (!GuideXosHost.TryCreate(&context,
                CounterApplicationFactory.StableApplicationId,
                out GuideXosHost host, out _ ) || host == null ||
            !runtime.TryGet(generation, out GuideXosC185ApplicationBridge bridge))
        {
            drawResult = ManagedUiResult.InvalidState;
            return ManagedUiResult.InvalidState;
        }

        GuideXosC185WindowAdapter window = bridge.ServicesForTests?.Window;
        if (window == null)
        {
            drawResult = ManagedUiResult.InvalidState;
            return ManagedUiResult.InvalidState;
        }
        ManagedUiResult closeResult = window.TryClose();
        drawResult = window.Surface.TryFillRect(0, 0, 4, 4, 0u);
        return closeResult;
    }

    private static bool RunStress()
    {
        bool passed = true;
        passed &= C186Case(RunServiceOperationStress());
        passed &= C186Case(RunResizeStress());
        passed &= C186Case(RunRejectedGeometryStress());
        passed &= C186Case(RunFailureRecoveryStressC186());
        passed &= C186Case(RunStaleCallbackStress());
        passed &= C186Case(RunLifecycleStressC186());
        return passed;
    }

    private static bool RunServiceOperationStress()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        bool allPassed = true;
        for (int index = 0; index < StressServiceOperationCountC186; index++)
            allPassed &= Invoke(runtime, generation, PointerDown(250, 140)) ==
                GuideXosResult.Success;
        C186ServiceOperations = StressServiceOperationCountC186;
        GuideXosC185ApplicationBridge bridge = runtime.CurrentBridgeForTests;
        bool counterValid = (bridge?.ApplicationForTests as CounterApplication)?.Counter ==
            StressServiceOperationCountC186;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        return allPassed && counterValid && closed;
    }

    private static bool RunResizeStress()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        if (Invoke(runtime, generation, ResizeAction(400, 240)) != GuideXosResult.Success ||
            Invoke(runtime, generation, ResizeAction(360, 200)) != GuideXosResult.Success)
            return false;
        long before = GC.GetAllocatedBytesForCurrentThread();
        bool allAccepted = true;
        for (int index = 0; index < StressValidResizeCountC186; index++)
        {
            bool wide = (index & 1) == 0;
            allAccepted &= Invoke(runtime, generation,
                ResizeAction(wide ? 400 : 360, wide ? 240 : 200)) ==
                GuideXosResult.Success;
        }
        C186ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        C186ValidResizes = StressValidResizeCountC186;
        GuideXosC185WindowAdapter window = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
        C186GeometryDrift = window == null || window.OuterWidth != 360 ||
            window.OuterHeight != 200 ? 1 : 0;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        return allAccepted && C186ResizeAllocationBytes == 0 &&
            C186GeometryDrift == 0 && closed;
    }

    private static bool RunRejectedGeometryStress()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        bool rejected = true;
        for (int index = 0; index < StressRejectedGeometryCountC186; index++)
            rejected &= Invoke(runtime, generation, ResizeAction(319, 240)) ==
                GuideXosResult.InvalidArgument;
        GuideXosC185WindowAdapter window = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
        bool rolledBack = window != null && window.OuterWidth == 360 &&
            window.OuterHeight == 200;
        C186RejectedGeometry = StressRejectedGeometryCountC186;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        return rejected && rolledBack && closed;
    }

    private static bool RunFailureRecoveryStressC186()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        bool recovered = true;
        for (int index = 0; index < StressFailureRecoveryCountC186; index++)
        {
            s_failNextRect = true;
            GuideXosResult failed = Invoke(runtime, 0u, 0u);
            bool failureReleased = failed == GuideXosResult.InvalidArgument &&
                runtime.CurrentBridgeForTests == null && s_closeRequests == 1 &&
                ActiveFakeWindowCount() == 0;
            GuideXosResult success = Invoke(runtime, 0u, 0u);
            GuideXosC185WindowAdapter window = runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
            uint generation = window?.Identity.Generation ?? 0u;
            bool recoveryClosed = success == GuideXosResult.Success && generation != 0u &&
                Invoke(runtime, generation,
                    Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                    GuideXosResult.Success;
            recovered &= failureReleased && recoveryClosed;
            ResetFakeHost();
        }
        C186FailureRecovery = StressFailureRecoveryCountC186;
        return recovered;
    }

    private static bool RunStaleCallbackStress()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        bool allRejected = true;
        for (int index = 0; index < StressStaleCallbackCountC186; index++)
        {
            if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success)
            {
                allRejected = false;
                break;
            }
            uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
            if (Invoke(runtime, generation,
                    Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) !=
                GuideXosResult.Success)
            {
                allRejected = false;
                break;
            }
            allRejected &= Invoke(runtime, generation,
                PointerDown(250, 140)) == GuideXosResult.InvalidArgument;
            ResetFakeHost();
        }
        C186StaleCallbacks = StressStaleCallbackCountC186;
        return allRejected;
    }

    private static bool RunLifecycleStressC186()
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        bool allClosed = true;
        for (int index = 0; index < StressLifecycleCountC186; index++)
        {
            if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success)
            {
                allClosed = false;
                break;
            }
            uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
            bool input = Invoke(runtime, generation, PointerDown(250, 140)) ==
                GuideXosResult.Success;
            bool resize = Invoke(runtime, generation,
                ResizeAction(400, 240)) == GuideXosResult.Success;
            bool close = Invoke(runtime, generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success;
            bool stale = Invoke(runtime, generation,
                PointerDown(250, 140)) == GuideXosResult.InvalidArgument;
            allClosed &= input && resize && close && stale &&
                runtime.CurrentBridgeForTests == null &&
                ActiveFakeWindowCount() == 0;
            ResetFakeHost();
        }
        C186Lifecycles = StressLifecycleCountC186;
        return allClosed;
    }

    private static bool C186Case(bool passed)
    {
        s_c186Cases++;
        if (!passed && s_c186FirstFailedCase == 0)
        {
            s_c186FirstFailedCase = s_c186Cases;
            s_c186FailureDetail = $"failed-case-{s_c186Cases}";
        }
        return passed;
    }

    private sealed class IsolatedApplication : IManagedUiApplication
    {
        private IManagedUiWindow _window;
        public int InputCount { get; private set; }
        public int TeardownCount { get; private set; }

        public ManagedUiResult Initialize(IManagedUiHostServices services,
            in ManagedUiApplicationIdentity identity)
        {
            _ = identity;
            ManagedUiResult result = services.TryCreateWindow("isolated"u8,
                100, 80, out _window);
            return result == ManagedUiResult.Success
                ? _window.Surface.TryFillRect(0, 0, 4, 4, 0u) : result;
        }

        public ManagedUiResult Render() => _window?.Surface.TryFillRect(
            0, 0, 4, 4, 0u) ?? ManagedUiResult.InvalidState;

        public ManagedUiResult HandleAction(uint actionId)
        {
            _ = actionId;
            return ManagedUiResult.Success;
        }

        public ManagedUiResult HandleInput(in ManagedUiInputEvent input)
        {
            _ = input;
            InputCount++;
            return ManagedUiResult.Success;
        }

        public bool TryGetResizeBounds(out ManagedUiResizeBounds bounds)
        {
            bounds = default;
            return false;
        }

        public ManagedUiResizeOutcome HandleResize(
            in ManagedUiResizeProposal proposal)
        {
            _ = proposal;
            return new(false, ManagedUiResult.Unsupported, false);
        }

        public void CompleteResize(bool accepted) => _ = accepted;
        public void OnTearingDown()
        {
            TeardownCount++;
            _window = null;
        }
    }

    private sealed class CloseOnInputApplication : IManagedUiApplication
    {
        private IManagedUiWindow _window;
        public ManagedUiResult CloseResult { get; private set; }
        public int TeardownCount { get; private set; }

        public ManagedUiResult Initialize(IManagedUiHostServices services,
            in ManagedUiApplicationIdentity identity)
        {
            _ = identity;
            return services.TryCreateWindow("close-on-input"u8,
                100, 80, out _window);
        }
        public ManagedUiResult Render() => ManagedUiResult.Success;
        public ManagedUiResult HandleAction(uint actionId) => ManagedUiResult.Success;
        public ManagedUiResult HandleInput(in ManagedUiInputEvent input)
        {
            _ = input;
            CloseResult = _window.TryClose();
            return CloseResult;
        }
        public bool TryGetResizeBounds(out ManagedUiResizeBounds bounds)
        {
            bounds = new(80, 48, 200, 120);
            return true;
        }
        public ManagedUiResizeOutcome HandleResize(in ManagedUiResizeProposal proposal) =>
            new(true, ManagedUiResult.Success, true);
        public void CompleteResize(bool accepted) { }
        public void OnTearingDown()
        {
            TeardownCount++;
            _window = null;
        }
    }

    private sealed class FailingInitializeApplication : IManagedUiApplication
    {
        private readonly bool _throws;
        public FailingInitializeApplication(bool throws) => _throws = throws;
        public IManagedUiWindow SavedWindow { get; private set; }
        public int TeardownCount { get; private set; }

        public ManagedUiResult Initialize(IManagedUiHostServices services,
            in ManagedUiApplicationIdentity identity)
        {
            _ = identity;
            ManagedUiResult result = services.TryCreateWindow("partial"u8,
                100, 80, out IManagedUiWindow window);
            SavedWindow = window;
            if (result != ManagedUiResult.Success) return result;
            if (_throws) throw new InvalidOperationException("init failure");
            return ManagedUiResult.ApplicationFailure;
        }

        public ManagedUiResult Render() => ManagedUiResult.Success;
        public ManagedUiResult HandleAction(uint actionId) => ManagedUiResult.Success;
        public ManagedUiResult HandleInput(in ManagedUiInputEvent input) => ManagedUiResult.Success;
        public bool TryGetResizeBounds(out ManagedUiResizeBounds bounds)
        {
            bounds = default;
            return false;
        }
        public ManagedUiResizeOutcome HandleResize(in ManagedUiResizeProposal proposal) =>
            new(false, ManagedUiResult.Unsupported, false);
        public void CompleteResize(bool accepted) { }
        public void OnTearingDown() => TeardownCount++;
    }

    private sealed class ThrowingResizeApplication : IManagedUiApplication
    {
        public IManagedUiWindow Window { get; private set; }
        public int CompleteResizeCalls { get; private set; }
        public bool LastResizeAccepted { get; private set; }

        public ManagedUiResult Initialize(IManagedUiHostServices services,
            in ManagedUiApplicationIdentity identity)
        {
            _ = identity;
            ManagedUiResult result = services.TryCreateWindow("resize"u8,
                100, 80, out IManagedUiWindow window);
            Window = window;
            return result;
        }
        public ManagedUiResult Render() => ManagedUiResult.Success;
        public ManagedUiResult HandleAction(uint actionId) => ManagedUiResult.Success;
        public ManagedUiResult HandleInput(in ManagedUiInputEvent input) => ManagedUiResult.Success;
        public bool TryGetResizeBounds(out ManagedUiResizeBounds bounds)
        {
            bounds = new(80, 48, 200, 120);
            return true;
        }
        public ManagedUiResizeOutcome HandleResize(in ManagedUiResizeProposal proposal)
        {
            _ = proposal;
            throw new InvalidOperationException("resize failure");
        }
        public void CompleteResize(bool accepted)
        {
            CompleteResizeCalls++;
            LastResizeAccepted = accepted;
        }
        public void OnTearingDown() => _ = Window?.TryClose();
    }
}
#endif
