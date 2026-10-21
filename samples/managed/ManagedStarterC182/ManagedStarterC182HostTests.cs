using System;
using System.Runtime.InteropServices;

namespace HostLogProof.Applications;

/// <summary>Hosted callback harness for the non-registered C182 example.</summary>
public static unsafe class ManagedStarterC182HostTests
{
    private const int MaximumTitleBytes = 128;
    private static readonly byte[] s_title = new byte[MaximumTitleBytes];
    private static int s_titleLength;
    private static int s_windowRequests;
    private static int s_lastWindowWidth;
    private static int s_lastWindowHeight;
    private static ulong s_lastWindow;
    private static int s_textDrawAttempts;
    private static int s_rectDraws;
    private static int s_closeCalls;
    private static bool s_rejectNextWindow;
    private static bool s_failNextTextDraw;
    private static int s_caseCount;
    private static int s_firstFailedCase;

    public static int CaseCount => s_caseCount;
    public static int FirstFailedCase => s_firstFailedCase;
    public static int ResizeTransactionCount { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }

    public static bool Run()
    {
        s_caseCount = 0;
        s_firstFailedCase = 0;
        ResizeTransactionCount = 0;
        ResizeAllocationBytes = -1;
        ResetHarness();

        NativeHostCallTable table = MakeTable();
        NativeGxAppContext context = MakeContext(&table);
        if (!GuideXosHost.TryCreate(&context, 4u, out GuideXosHost host,
                out GuideXosResult hostResult) ||
            hostResult != GuideXosResult.Success)
        {
            return Check(false);
        }

        NonResizableProbe nonResizable = new();
        Check(!nonResizable.TryGetResizeGeometry());

        ManagedStarterC182 app = new();
        bool launched = app.Launch(host) == GuideXosResult.Success &&
            app.HasSurface && app.WindowHandle == s_lastWindow &&
            s_windowRequests == 1 && s_lastWindowWidth ==
                ManagedStarterC182.DefaultOuterWidth &&
            s_lastWindowHeight == ManagedStarterC182.DefaultOuterHeight &&
            BytesEqual(s_title, s_titleLength, "Managed Starter"u8) &&
            app.StatusText == "Activations: 0" &&
            app.RegisteredControlCount == 1 && s_textDrawAttempts >= 2;
        Check(launched);

        bool resizeOptIn = app.TryGetManagedResizeGeometryC180(
                out GuideXosManagedWindowResizeBoundsC174 bounds,
                out int width, out int height) &&
            width == ManagedStarterC182.DefaultOuterWidth &&
            height == ManagedStarterC182.DefaultOuterHeight &&
            bounds.MinimumOuterWidth == ManagedStarterC182.MinimumOuterWidth &&
            bounds.MinimumOuterHeight == ManagedStarterC182.MinimumOuterHeight &&
            bounds.MaximumOuterWidth == ManagedStarterC182.MaximumOuterWidth &&
            bounds.MaximumOuterHeight == ManagedStarterC182.MaximumOuterHeight;
        Check(resizeOptIn);

        int buttonX = app.ButtonX;
        int buttonY = app.ButtonY;
        GuideXosInputEvent pointer = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            buttonX + 4, buttonY + 4);
        bool pointerActivation = app.HandleInput(host, pointer) ==
                GuideXosResult.Success && app.ActivationCount == 1 &&
            app.StatusText == "Activations: 1" &&
            app.WindowHandle == s_lastWindow;
        GuideXosInputEvent pointerOutside = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            buttonX + 200, buttonY + 4);
        pointerActivation &= app.HandleInput(host, pointerOutside) ==
                GuideXosResult.Success && app.ActivationCount == 1;
        Check(pointerActivation);

        GuideXosInputEvent tab = GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Tab);
        GuideXosInputEvent enter = GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter);
        bool keyboardActivation = app.HandleInput(host, tab) ==
                GuideXosResult.Success &&
            app.HandleInput(host, enter) == GuideXosResult.Success &&
            app.ActivationCount == 2 && app.StatusText == "Activations: 2";
        GuideXosInputEvent spaceDown = GuideXosInputEvent.ForKeyDown(
            (GuideXosTextInputKey)' ');
        GuideXosInputEvent spaceChar = GuideXosInputEvent.ForKeyChar(' ');
        keyboardActivation &= app.HandleInput(host, spaceDown) ==
                GuideXosResult.Success &&
            app.HandleInput(host, spaceChar) == GuideXosResult.Success &&
            app.ActivationCount == 3 && app.StatusText == "Activations: 3";
        Check(keyboardActivation);

        bool managedAction = app.HandleAction(host,
                ManagedStarterC182.IncrementActionId) == GuideXosResult.Success &&
            app.ActivationCount == 4 && app.StatusText == "Activations: 4";
        bool rejectedAction = app.HandleAction(host, 0x7FFFu) ==
                GuideXosResult.InvalidAction && app.ActivationCount == 4;
        s_failNextTextDraw = true;
        bool failedActionRollback = app.HandleAction(host,
                ManagedStarterC182.IncrementActionId) ==
                GuideXosResult.InvalidArgument && app.ActivationCount == 4 &&
            app.StatusText == "Activations: 4";
        Check(managedAction && rejectedAction && failedActionRollback);

        bool encodedResize = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            480, 260, out uint acceptedResize);
        GuideXosResult acceptedResizeResult = encodedResize
            ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                app, host, acceptedResize)
            : GuideXosResult.InvalidArgument;
        bool acceptedGeometry = acceptedResizeResult == GuideXosResult.Success &&
            app.OuterWidth == 480 && app.OuterHeight == 260 &&
            app.StatusX == 16 && app.StatusWidth == 448 &&
            app.ButtonX == 344 && app.ButtonY == 192 &&
            app.WindowHandle == s_lastWindow && app.ActivationCount == 4;
        Check(acceptedGeometry);

        int drawCountBeforeInvalid = s_textDrawAttempts;
        int invalidOldX = app.ButtonX;
        int invalidOldWidth = app.OuterWidth;
        bool encodedInvalid = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            319, 260, out uint invalidResize);
        GuideXosResult invalidResizeResult = encodedInvalid
            ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                app, host, invalidResize)
            : GuideXosResult.Success;
        bool invalidGeometryRejected = encodedInvalid &&
            invalidResizeResult == GuideXosResult.InvalidArgument &&
            app.ButtonX == invalidOldX && app.OuterWidth == invalidOldWidth &&
            s_textDrawAttempts == drawCountBeforeInvalid;
        Check(invalidGeometryRejected);

        int rollbackWidth = app.OuterWidth;
        int rollbackHeight = app.OuterHeight;
        int rollbackStatusWidth = app.StatusWidth;
        int rollbackButtonX = app.ButtonX;
        int rollbackButtonY = app.ButtonY;
        s_failNextTextDraw = true;
        bool encodedRejectedResize =
            GuideXosManagedWindowGeometryC172.TryEncodeAction(
                500, 280, out uint rejectedResize);
        GuideXosResult rejectedResizeResult = encodedRejectedResize
            ? GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                app, host, rejectedResize)
            : GuideXosResult.InvalidArgument;
        bool resizeRollback = rejectedResizeResult ==
                GuideXosResult.InvalidArgument && app.OuterWidth == rollbackWidth &&
            app.OuterHeight == rollbackHeight &&
            app.StatusWidth == rollbackStatusWidth &&
            app.ButtonX == rollbackButtonX && app.ButtonY == rollbackButtonY &&
            app.ActivationCount == 4;
        Check(resizeRollback);

        GuideXosManagedWindowGeometryC172.TryEncodeAction(
            500, 280, out uint resizeA);
        GuideXosManagedWindowGeometryC172.TryEncodeAction(
            480, 260, out uint resizeB);
        _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(app, host, resizeA);
        _ = GuideXosManagedWindowResizeDispatchC180.DispatchAction(app, host, resizeB);
        long allocationStart = GC.GetAllocatedBytesForCurrentThread();
        const int resizeIterations = 100;
        for (int index = 0; index < resizeIterations; index++)
        {
            uint action = (index & 1) == 0 ? resizeA : resizeB;
            GuideXosResult resizeResult =
                GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                    app, host, action);
            if (resizeResult != GuideXosResult.Success)
            {
                s_firstFailedCase = s_caseCount + 1;
                break;
            }
            ResizeTransactionCount++;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocationStart;
        bool allocationFreeResize = ResizeTransactionCount == resizeIterations &&
            ResizeAllocationBytes == 0 && app.WindowHandle == s_lastWindow &&
            app.ActivationCount == 4;
        Check(allocationFreeResize);

        bool closeRequest = host.TryGetSurface(app.WindowHandle,
                out GuideXosSurface closingSurface) == GuideXosResult.Success &&
            closingSurface.TryClose() == GuideXosResult.Success &&
            s_closeCalls == 1;
        app.OnTearingDown();
        bool teardown = closeRequest && !app.HasSurface &&
            app.ActivationCount == 0 && app.RegisteredControlCount == 0 &&
            !app.TryGetManagedResizeGeometryC180(out _, out _, out _);
        Check(teardown);

        s_rejectNextWindow = true;
        ManagedStarterC182 noWindowApp = new();
        bool noWindow = noWindowApp.Launch(host) ==
                GuideXosResult.SurfaceCreationFailed && !noWindowApp.HasSurface &&
            s_closeCalls == 1;
        Check(noWindow);

        s_failNextTextDraw = true;
        ManagedStarterC182 partialApp = new();
        bool partialInitialization = partialApp.Launch(host) ==
                GuideXosResult.InvalidArgument && !partialApp.HasSurface &&
            partialApp.RegisteredControlCount == 0 && s_closeCalls == 2 &&
            !partialApp.TryGetManagedResizeGeometryC180(out _, out _, out _);
        Check(partialInitialization);

        Check(RepeatedLifecycle(host));
#if HOSTLOGPROOF_MANAGED_APP_RETURN
        Check(LifetimeCapacityAndReuse());
        const int expectedCaseCount = 15;
#else
        const int expectedCaseCount = 14;
#endif

        return s_firstFailedCase == 0 && s_caseCount == expectedCaseCount;
    }

    private static bool RepeatedLifecycle(GuideXosHost host)
    {
        int startingRequests = s_windowRequests;
        int startingCloses = s_closeCalls;
        const int cycles = 64;
        bool passed = true;
        for (int index = 0; index < cycles; index++)
        {
            ManagedStarterC182 app = new();
            passed &= app.Launch(host) == GuideXosResult.Success && app.HasSurface;
            if (host.TryGetSurface(app.WindowHandle,
                    out GuideXosSurface surface) != GuideXosResult.Success ||
                surface.TryClose() != GuideXosResult.Success)
            {
                passed = false;
                break;
            }
            app.OnTearingDown();
            passed &= !app.HasSurface && app.RegisteredControlCount == 0 &&
                app.ActivationCount == 0 &&
                !app.TryGetManagedResizeGeometryC180(out _, out _, out _);
        }
        return passed && s_windowRequests - startingRequests == cycles &&
            s_closeCalls - startingCloses == cycles;
    }

#if HOSTLOGPROOF_MANAGED_APP_RETURN
    private static bool LifetimeCapacityAndReuse()
    {
        GuideXosManagedApplicationLifetime lifetime = new();
        GuideXosApplication[] residents = new GuideXosApplication[
            GuideXosManagedApplicationLifetime.Capacity];
        uint[] generations = new uint[GuideXosManagedApplicationLifetime.Capacity];
        bool started = true;
        for (int index = 0; index < residents.Length; index++)
        {
            uint selector = (uint)(4 + index);
            GuideXosApplicationDescriptor descriptor = new(selector,
                "C182 hosted starter"u8,
                static () => new ManagedStarterC182());
            started &= lifetime.TryStart(descriptor, (uint)(101 + index),
                out residents[index], out generations[index]) &&
                residents[index] is ManagedStarterC182;
        }

        int sixthFactoryCalls = 0;
        GuideXosApplicationDescriptor sixth = new(9u,
            "C182 host-only capacity probe"u8,
            () => { sixthFactoryCalls++; return new ManagedStarterC182(); });
        bool exhausted = !lifetime.TryStart(sixth, 106u, out _, out _) &&
            sixthFactoryCalls == 0;
        bool staleRejected = !lifetime.TryGet(4u, generations[0] + 1u, out _) &&
            !lifetime.Clear(4u, generations[0] + 1u);
        bool released = lifetime.Clear(4u, generations[0]) &&
            !lifetime.TryGet(4u, generations[0], out _);
        GuideXosApplicationDescriptor replacementDescriptor = new(4u,
            "C182 hosted starter replacement"u8,
            static () => new ManagedStarterC182());
        bool reused = lifetime.TryStart(replacementDescriptor, 107u,
                out GuideXosApplication replacement, out uint newGeneration) &&
            newGeneration == 107u &&
            replacement is ManagedStarterC182 &&
            !lifetime.TryGet(4u, generations[0], out _) &&
            !lifetime.Clear(4u, generations[0]) &&
            lifetime.TryGet(4u, newGeneration,
                out GuideXosApplication replacementCurrent) &&
            ReferenceEquals(replacement, replacementCurrent);

        bool survivorsStable = true;
        for (int index = 1; index < residents.Length; index++)
        {
            uint selector = (uint)(4 + index);
            survivorsStable &= lifetime.TryGet(selector, generations[index],
                out GuideXosApplication current) &&
                ReferenceEquals(residents[index], current);
        }

        bool cleared = lifetime.Clear(4u, newGeneration);
        for (int index = 1; index < residents.Length; index++)
            cleared &= lifetime.Clear((uint)(4 + index), generations[index]);
        return started && exhausted && staleRejected && released && reused &&
            survivorsStable && cleared;
    }
#endif

    private static NativeHostCallTable MakeTable()
    {
        NativeHostCallTable table = default;
        table.size = GxAbi.HostCallTableSize;
        table.version = GxAbi.HostAbiVersion;
        table.capabilities = (ulong)(GuideXosCapability.Surface |
            GuideXosCapability.Text | GuideXosCapability.Primitive |
            GuideXosCapability.Close);
        table.requestWindow = &RequestWindow;
        table.drawText = &DrawText;
        table.drawRect = &DrawRect;
        table.closeWindow = &CloseWindow;
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

    private static void ResetHarness()
    {
        s_titleLength = 0;
        s_windowRequests = 0;
        s_lastWindowWidth = 0;
        s_lastWindowHeight = 0;
        s_lastWindow = 0u;
        s_textDrawAttempts = 0;
        s_rectDraws = 0;
        s_closeCalls = 0;
        s_rejectNextWindow = false;
        s_failNextTextDraw = false;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        if (!condition && s_firstFailedCase == 0)
            s_firstFailedCase = s_caseCount;
        return condition;
    }

    private static bool BytesEqual(byte[] actual, int actualLength,
        ReadOnlySpan<byte> expected)
    {
        if (actualLength != expected.Length) return false;
        for (int index = 0; index < expected.Length; index++)
            if (actual[index] != expected[index]) return false;
        return true;
    }

    private static int ReadLength(byte* value, int maximum)
    {
        if (value == null) return -1;
        int length = 0;
        while (length < maximum && value[length] != 0) length++;
        return length;
    }

    [UnmanagedCallersOnly]
    private static int RequestWindow(NativeGxAppContext* context,
        byte* title, int width, int height, ulong* window)
    {
        if (window == null) return -2;
        if (s_rejectNextWindow)
        {
            s_rejectNextWindow = false;
            *window = 0u;
            return -1;
        }

        int length = ReadLength(title, MaximumTitleBytes - 1);
        if (length < 1) return -2;
        for (int index = 0; index < length; index++)
            s_title[index] = title[index];
        s_titleLength = length;
        s_lastWindowWidth = width;
        s_lastWindowHeight = height;
        s_lastWindow = 0xC1820000ul + (uint)++s_windowRequests;
        *window = s_lastWindow;
        return 0;
    }

    [UnmanagedCallersOnly]
    private static int DrawText(NativeGxAppContext* context, ulong window,
        int x, int y, byte* text)
    {
        s_textDrawAttempts++;
        if (s_failNextTextDraw)
        {
            s_failNextTextDraw = false;
            return -1;
        }
        return ReadLength(text, 128) >= 0 && window == s_lastWindow ? 0 : -1;
    }

    [UnmanagedCallersOnly]
    private static int DrawRect(NativeGxAppContext* context, ulong window,
        int x, int y, int width, int height, uint color)
    {
        s_rectDraws++;
        return window == s_lastWindow && width > 0 && height > 0 ? 0 : -1;
    }

    [UnmanagedCallersOnly]
    private static int CloseWindow(NativeGxAppContext* context, ulong window)
    {
        if (window != s_lastWindow) return -1;
        s_closeCalls++;
        return 0;
    }

    private sealed class NonResizableProbe : GuideXosApplication
    {
        public override GuideXosResult Launch(GuideXosHost host) =>
            GuideXosResult.Success;

        public bool TryGetResizeGeometry() => TryGetManagedResizeGeometryC180(
            out _, out _, out _);
    }
}
