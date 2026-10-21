#if HOSTLOGPROOF_C185_BOUNDARY
using System;
using GuideXos.C185.CounterApp;
using GuideXos.ManagedUi;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HostLogProof;

public static unsafe class GuideXosManagedHostServicesC185Tests
{
    public const int FocusedCaseCount = 28;
    public const int StressOperationCount = 1000;
    public const int StressResizeCount = 500;
    public const int StressRejectedRequestCount = 250;
    public const int StressFailureRecoveryCount = 250;
    public const int StressLifecycleCount = 100;

    private static int s_windowRequests;
    private static int s_textDraws;
    private static int s_rectDraws;
    private static int s_closeRequests;
    private static int s_lastWidth;
    private static int s_lastHeight;
    private static bool s_failNextWindow;
    private static bool s_failNextRect;
    private static int s_lifetimeFactoryCalls;

    public static int FirstFailedCase { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }
    public static long ServiceOperationAllocationBytes { get; private set; }
    public static int ActualServiceOperations { get; private set; }
    public static int ActualResizeTransactions { get; private set; }
    public static int ActualRejectedRequests { get; private set; }
    public static int ActualFailureRecoveryOperations { get; private set; }
    public static int ActualLifecycles { get; private set; }
    public static int NativeTextDrawCount => s_textDraws;
    public static int NativeRectDrawCount => s_rectDraws;
    public static int NativeWindowCreateCount => s_windowRequests;
    public static int NativeWindowCloseCount => s_closeRequests;
    public static string FailureDetail { get; private set; } = string.Empty;

    public static bool Run()
    {
        FirstFailedCase = 0;
        FailureDetail = string.Empty;
        ResizeAllocationBytes = -1;
        ServiceOperationAllocationBytes = -1;
        ActualServiceOperations = 0;
        ActualResizeTransactions = 0;
        ActualRejectedRequests = 0;
        ActualFailureRecoveryOperations = 0;
        ActualLifecycles = 0;
        int cases = 0;
        bool passed = true;

        ResetFakeHost();
        ManagedUiApplicationIdentity identity = new(
            CounterApplicationFactory.StableApplicationId, 1u);
        CounterApplication missingServices = new();
        passed &= Case(ref cases, missingServices.Initialize(null, in identity) ==
            ManagedUiResult.InvalidArgument);
        passed &= Case(ref cases,
            GuideXosManagedHostServicesC185Composition.RegisteredFactoryCount == 1 &&
            GuideXosManagedHostServicesC185Composition.HasFactory(
                CounterApplicationFactory.StableApplicationId) &&
            !GuideXosManagedHostServicesC185Composition.HasFactory(0u));
        passed &= Case(ref cases, RunInvalidHostServiceRequests());

        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        passed &= Case(ref cases, Invoke(runtime, 0u, 0u) == GuideXosResult.Success);
        GuideXosC185ApplicationBridge bridge = runtime.CurrentBridgeForTests;
        GuideXosC185WindowAdapter hostWindow = bridge?.ServicesForTests?.Window;
        CounterApplication counter = bridge?.ApplicationForTests as CounterApplication;
        passed &= Case(ref cases, bridge != null && counter != null && hostWindow != null &&
            s_windowRequests == 1 && s_lastWidth == 360 && s_lastHeight == 200);
        passed &= Case(ref cases, hostWindow != null && counter != null &&
            hostWindow.Identity.ApplicationId == CounterApplicationFactory.StableApplicationId &&
            hostWindow.Identity.Generation != 0u &&
            hostWindow.Identity.Ordinal == 1u && counter.Counter == 0);
        ManagedUiResult manualFillResult = hostWindow == null
            ? ManagedUiResult.InvalidState
            : InvokeSurfaceFill(runtime, generation: hostWindow.Identity.Generation);
        bool drawPassed = manualFillResult == ManagedUiResult.Success &&
            s_rectDraws > 0 && s_textDraws >= 2;
        if (!drawPassed)
            FailureDetail = $"surface={manualFillResult} rect={s_rectDraws} text={s_textDraws}";
        passed &= Case(ref cases, hostWindow != null && drawPassed);
        passed &= Case(ref cases, hostWindow != null &&
            hostWindow.Surface.TryFillRect(-1, 0, 8, 8, 0u) ==
                ManagedUiResult.InvalidArgument &&
            hostWindow.Surface.TrySetClip(350, 190, 20, 20) ==
                ManagedUiResult.InvalidArgument);
        passed &= Case(ref cases, hostWindow != null &&
            bridge.ServicesForTests.TryCreateWindow("Second"u8, 320, 180,
                out _) == ManagedUiResult.ResourceLimit);
        passed &= Case(ref cases, hostWindow != null &&
            hostWindow.TryCreateButton(2u, 10, 10, 100, 24, "Other"u8,
                out _) == ManagedUiResult.ResourceLimit);

        uint generation = hostWindow?.Identity.Generation ?? 0u;
        passed &= Case(ref cases, Invoke(runtime, generation,
            PointerDown(250, 140)) == GuideXosResult.Success &&
            counter?.Counter == 1 && s_textDraws > 2);
        passed &= Case(ref cases, Invoke(runtime, generation,
            KeyDown(GuideXosTextInputKey.Enter)) == GuideXosResult.Success &&
            counter?.Counter == 2);
        passed &= Case(ref cases, Invoke(runtime, generation,
            KeyCharacter(' ')) == GuideXosResult.Success &&
            counter?.Counter == 3);

        ManagedUiWindowIdentity originalIdentity = hostWindow?.Identity ?? default;
        passed &= Case(ref cases, Invoke(runtime, generation,
            ResizeAction(400, 240)) == GuideXosResult.Success &&
            hostWindow?.OuterWidth == 400 && hostWindow.OuterHeight == 240 &&
            hostWindow.Identity.ApplicationId == originalIdentity.ApplicationId &&
            hostWindow.Identity.Generation == originalIdentity.Generation &&
            hostWindow.Identity.Ordinal == originalIdentity.Ordinal);
        passed &= Case(ref cases, Invoke(runtime, generation,
            ResizeAction(319, 240)) == GuideXosResult.InvalidArgument &&
            hostWindow?.OuterWidth == 400 && hostWindow.OuterHeight == 240);
        s_failNextRect = true;
        passed &= Case(ref cases, Invoke(runtime, generation,
            ResizeAction(480, 300)) == GuideXosResult.InvalidArgument &&
            hostWindow?.OuterWidth == 400 && hostWindow.OuterHeight == 240 &&
            hostWindow.Identity.Generation == originalIdentity.Generation);
        passed &= Case(ref cases, Invoke(runtime, generation + 1u,
            PointerDown(280, 180)) == GuideXosResult.InvalidArgument &&
            counter?.Counter == 3);

        passed &= Case(ref cases, RunDefaultDenyResizeCheck());
        passed &= Case(ref cases, RunLifetimePoolBoundaryChecks());

        IManagedUiWindow releasedWindow = hostWindow;
        GuideXosC185ApplicationBridge releasedBridge = bridge;
        int closedBefore = s_closeRequests;
        passed &= Case(ref cases, Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success && runtime.CurrentBridgeForTests == null &&
            s_closeRequests == closedBefore + 1);
        passed &= Case(ref cases, releasedWindow != null &&
            releasedWindow.Surface.TryFillRect(0, 0, 4, 4, 0u) ==
                ManagedUiResult.InvalidState &&
            releasedWindow.RequestRedraw() == ManagedUiResult.InvalidState &&
            releasedWindow.TryClose() == ManagedUiResult.Success &&
            s_closeRequests == closedBefore + 1);
        releasedBridge?.OnTearingDown();
        passed &= Case(ref cases, s_closeRequests == closedBefore + 1);

        passed &= RunResizeAllocationStress(ref cases, ref passed);
        passed &= RunServiceOperationStress(ref cases, ref passed);
        passed &= RunRejectedRequestStress(ref cases, ref passed);
        passed &= RunFailureRecoveryStress(ref cases, ref passed);
        passed &= RunLifecycleStress(ref cases, ref passed);

        passed &= Case(ref cases, cases + 1 == FocusedCaseCount);
        return passed && FirstFailedCase == 0;
    }

    [UnmanagedCallersOnly]
    private static int RequestWindow(NativeGxAppContext* context,
        byte* title, int width, int height, ulong* window)
    {
        _ = context;
        s_windowRequests++;
        s_lastWidth = width;
        s_lastHeight = height;
        if (s_failNextWindow)
        {
            s_failNextWindow = false;
            return -1;
        }
        if (title == null || title[0] == 0 || window == null ||
            width <= 0 || height <= 0)
            return -1;
        *window = 0xC185u;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int DrawText(NativeGxAppContext* context, ulong window,
        int x, int y, byte* text)
    {
        _ = context;
        if (window == 0u || x < 0 || y < 0 || text == null) return -1;
        s_textDraws++;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int DrawRect(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, uint color)
    {
        _ = context;
        _ = color;
        if (window == 0u || x < 0 || y < 0 || width <= 0 || height <= 0)
            return -1;
        s_rectDraws++;
        if (!s_failNextRect) return 0;
        s_failNextRect = false;
        return -1;
    }

    [UnmanagedCallersOnly]
    private static int CloseWindow(NativeGxAppContext* context, ulong window)
    {
        _ = context;
        if (window == 0u) return -1;
        s_closeRequests++;
        return 0;
    }

    private static bool RunInvalidHostServiceRequests()
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, 0u, 0u);
        if (!GuideXosHost.TryCreate(&context,
                CounterApplicationFactory.StableApplicationId,
                out GuideXosHost host, out GuideXosResult hostResult) || host == null)
            return false;
        GuideXosC185ApplicationBridge owner = new(new CounterApplication());
        GuideXosC185HostServicesAdapter services = new(host, owner,
            new ManagedUiApplicationIdentity(
                CounterApplicationFactory.StableApplicationId, 1u));
        bool invalidTitle = services.TryCreateWindow(ReadOnlySpan<byte>.Empty,
            360, 200, out _) == ManagedUiResult.InvalidArgument;
        bool invalidGeometry = services.TryCreateWindow("bad"u8,
            0, 200, out _) == ManagedUiResult.InvalidArgument;
        services.Release();
        return invalidTitle && invalidGeometry &&
            GuideXosC185ResultMap.ToManaged(hostResult) == ManagedUiResult.Success;
    }

    private static bool RunDefaultDenyResizeCheck()
    {
        NoResizeApplication application = new();
        bool encoded = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            400, 240, out uint actionId);
        return encoded &&
            GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                application, null, actionId) == GuideXosResult.InvalidAction &&
            application.ResizeCalls == 0;
    }

    private static bool RunLifetimePoolBoundaryChecks()
    {
        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplicationDescriptor[] descriptors = new GuideXosApplicationDescriptor[6];
        GuideXosApplication[] active = new GuideXosApplication[5];
        uint[] generations = new uint[5];
        for (int index = 0; index < 5; index++)
        {
            uint applicationId = (uint)(0xC1851000 + index);
            descriptors[index] = new GuideXosApplicationDescriptor(
                applicationId, "C185 lifecycle probe"u8,
                static () => CreateLifetimeProbe());
            if (!lifetime.TryStart(descriptors[index], (uint)(100 + index),
                    out active[index], out generations[index]))
                return false;
        }

        descriptors[5] = new GuideXosApplicationDescriptor(0xC1851005u,
            "C185 lifecycle probe"u8, static () => CreateLifetimeProbe());
        int factoryCallsBefore = s_lifetimeFactoryCalls;
        bool rejectedAtCapacity = !lifetime.TryStart(descriptors[5],
            out _, out _) && s_lifetimeFactoryCalls == factoryCallsBefore;
        bool cleared = lifetime.Clear(descriptors[0].Selector, generations[0]);
        bool staleRejected = !lifetime.TryGet(descriptors[0].Selector,
            generations[0], out _);
        bool slotReused = lifetime.TryStart(descriptors[5], out _,
            out uint replacementGeneration);
        return rejectedAtCapacity && cleared && staleRejected && slotReused &&
            replacementGeneration > generations[4];
    }

    private static bool RunResizeAllocationStress(ref int cases,
        ref bool passed)
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        _ = Invoke(runtime, generation, ResizeAction(400, 240));
        _ = Invoke(runtime, generation, ResizeAction(360, 200));
        long before = GC.GetAllocatedBytesForCurrentThread();
        bool allAccepted = true;
        for (int index = 0; index < StressResizeCount; index++)
        {
            bool wide = (index & 1) == 0;
            GuideXosResult result = Invoke(runtime, generation,
                ResizeAction(wide ? 400 : 360, wide ? 240 : 200));
            allAccepted &= result == GuideXosResult.Success;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        ActualResizeTransactions = StressResizeCount;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        bool measurementPassed = allAccepted && closed && ResizeAllocationBytes == 0;
        passed &= Case(ref cases, measurementPassed);
        return measurementPassed;
    }

    private static bool RunServiceOperationStress(ref int cases,
        ref bool passed)
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        _ = Invoke(runtime, generation, GxAbi.LaunchFlagInput);
        long before = GC.GetAllocatedBytesForCurrentThread();
        bool allDispatched = true;
        for (int index = 0; index < StressOperationCount; index++)
            allDispatched &= Invoke(runtime, generation, GxAbi.LaunchFlagInput) ==
                GuideXosResult.Success;
        ServiceOperationAllocationBytes = GC.GetAllocatedBytesForCurrentThread() - before;
        ActualServiceOperations = StressOperationCount;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        bool measurementPassed = allDispatched && closed &&
            ServiceOperationAllocationBytes == 0;
        passed &= Case(ref cases, measurementPassed);
        return measurementPassed;
    }

    private static bool RunRejectedRequestStress(ref int cases,
        ref bool passed)
    {
        ResetFakeHost();
        GuideXosC185Runtime runtime =
            GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
        if (Invoke(runtime, 0u, 0u) != GuideXosResult.Success) return false;
        uint generation = runtime.CurrentBridgeForTests.ServicesForTests.Window.Identity.Generation;
        bool allRejected = true;
        for (int index = 0; index < StressRejectedRequestCount; index++)
            allRejected &= Invoke(runtime, generation,
                ResizeAction(319, 240)) == GuideXosResult.InvalidArgument;
        ActualRejectedRequests = StressRejectedRequestCount;
        bool stable = runtime.TryGet(generation,
            out GuideXosC185ApplicationBridge bridge) &&
            bridge.ServicesForTests.Window.OuterWidth == 360 &&
            bridge.ServicesForTests.Window.OuterHeight == 200;
        bool closed = Invoke(runtime, generation,
            Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
            GuideXosResult.Success;
        bool result = allRejected && stable && closed;
        passed &= Case(ref cases, result);
        return result;
    }

    private static bool RunFailureRecoveryStress(ref int cases,
        ref bool passed)
    {
        bool allRecovered = true;
        for (int index = 0; index < StressFailureRecoveryCount; index++)
        {
            ResetFakeHost();
            GuideXosC185Runtime runtime =
                GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
            s_failNextRect = true;
            GuideXosResult failedLaunch = Invoke(runtime, 0u, 0u);
            bool failedAndReleased = failedLaunch == GuideXosResult.InvalidArgument &&
                runtime.CurrentBridgeForTests == null && s_closeRequests == 1;
            GuideXosResult recoveredLaunch = Invoke(runtime, 0u, 0u);
            GuideXosC185ApplicationBridge bridge = runtime.CurrentBridgeForTests;
            uint generation = bridge?.ServicesForTests?.Window?.Identity.Generation ?? 0u;
            bool recovered = recoveredLaunch == GuideXosResult.Success && generation != 0u;
            bool closed = recovered && Invoke(runtime, generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success;
            allRecovered &= failedAndReleased && recovered && closed;
        }
        ActualFailureRecoveryOperations = StressFailureRecoveryCount;
        passed &= Case(ref cases, allRecovered);
        return allRecovered;
    }

    private static bool RunLifecycleStress(ref int cases,
        ref bool passed)
    {
        bool allClosed = true;
        for (int index = 0; index < StressLifecycleCount; index++)
        {
            ResetFakeHost();
            GuideXosC185Runtime runtime =
                GuideXosManagedHostServicesC185Composition.CreateRuntimeForTests();
            GuideXosResult launched = Invoke(runtime, 0u, 0u);
            GuideXosC185WindowAdapter window =
                runtime.CurrentBridgeForTests?.ServicesForTests?.Window;
            uint generation = window?.Identity.Generation ?? 0u;
            GuideXosResult input = Invoke(runtime, generation,
                PointerDown(250, 140));
            GuideXosResult closed = Invoke(runtime, generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId));
            allClosed &= launched == GuideXosResult.Success &&
                input == GuideXosResult.Success && closed == GuideXosResult.Success &&
                runtime.CurrentBridgeForTests == null;
        }
        ActualLifecycles = StressLifecycleCount;
        passed &= Case(ref cases, allClosed);
        return allClosed;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static GuideXosResult Invoke(GuideXosC185Runtime runtime,
        uint generation, uint launchFlags)
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation, launchFlags);
        if (!GuideXosHost.TryCreate(&context,
                CounterApplicationFactory.StableApplicationId,
                out GuideXosHost host, out GuideXosResult hostResult) || host == null)
            return hostResult;
        return runtime.Dispatch(host);
    }

    private static ManagedUiResult InvokeSurfaceFill(
        GuideXosC185Runtime runtime, uint generation)
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation,
            GxAbi.LaunchFlagInput);
        if (!GuideXosHost.TryCreate(&context,
                CounterApplicationFactory.StableApplicationId,
                out GuideXosHost host, out _) || host == null)
            return ManagedUiResult.InvalidState;
        if (runtime.Dispatch(host) != GuideXosResult.Success ||
            !runtime.TryGet(generation,
                out GuideXosC185ApplicationBridge bridge) ||
            bridge.ServicesForTests?.Window == null)
            return ManagedUiResult.InvalidState;
        return bridge.ServicesForTests.Window.Surface.TryFillRect(
            0, 0, 8, 8, 0xFF010203u);
    }

    private static NativeHostCallTable CreateCallTable()
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableV1Size;
        table.version = GxAbi.HostAbiV1Version;
        table.requestWindow = &RequestWindow;
        table.drawText = &DrawText;
        table.drawRect = &DrawRect;
        table.closeWindow = &CloseWindow;
        table.capabilities = (ulong)(GuideXosCapability.Surface |
            GuideXosCapability.Text | GuideXosCapability.Primitive |
            GuideXosCapability.Close);
        return table;
    }

    private static NativeGxAppContext CreateContext(NativeHostCallTable* table,
        uint generation, uint launchFlags)
    {
        NativeGxAppContext context = default;
        context.size = GxAbi.AppContextLaunchPrefixSize;
        context.apiVersion = GxAbi.ApiVersion;
        context.host = table;
        context.userData = (void*)(nuint)((ulong)generation << 32);
        context.launchFlags = launchFlags;
        return context;
    }

    private static uint PointerDown(int x, int y) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputPointerDown |
        ((uint)x & GxAbi.LaunchFlagInputCoordinateMask) |
        (((uint)y & GxAbi.LaunchFlagInputCoordinateMask) << 12);

    private static uint KeyDown(GuideXosTextInputKey key) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyDown | (uint)key;

    private static uint KeyCharacter(char character) =>
        GxAbi.LaunchFlagInput | GxAbi.LaunchFlagInputKeyChar | character;

    private static uint ResizeAction(int width, int height)
    {
        return GuideXosManagedWindowGeometryC172.TryEncodeAction(width, height,
            out uint actionId) ? GxAbi.LaunchFlagAction | actionId :
            GxAbi.LaunchFlagAction;
    }

    private static uint Action(uint actionId) =>
        GxAbi.LaunchFlagAction | actionId;

    private static GuideXosApplication CreateLifetimeProbe()
    {
        s_lifetimeFactoryCalls++;
        return new C185LifetimeProbe();
    }

    private static void ResetFakeHost()
    {
        s_windowRequests = 0;
        s_textDraws = 0;
        s_rectDraws = 0;
        s_closeRequests = 0;
        s_lastWidth = 0;
        s_lastHeight = 0;
        s_failNextWindow = false;
        s_failNextRect = false;
    }

    private static bool Case(ref int count, bool condition)
    {
        count++;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = count;
        return condition;
    }

    private sealed class NoResizeApplication : GuideXosApplication
    {
        internal int ResizeCalls { get; private set; }
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;
        internal override GuideXosResult HandleManagedResizeC180(GuideXosHost host,
            ref GuideXosManagedWindowResizeTransactionC174 transaction)
        {
            _ = host;
            _ = transaction;
            ResizeCalls++;
            return GuideXosResult.Success;
        }
    }

    private sealed class C185LifetimeProbe : GuideXosApplication
    {
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;

    }
}
#endif
