#if HOSTLOGPROOF_C188_EXTERNAL_APP_IDENTITY
using System;
using GuideXos.C185.CounterApp;
using GuideXos.ManagedUi;

namespace HostLogProof;

public static unsafe partial class GuideXosManagedHostServicesC185Tests
{
    public const int FocusedCaseCountC188 = 25;
    public static int C188FirstFailedCase { get; private set; }
    public static string C188FailureDetail { get; private set; } = string.Empty;
    public static long C188ResizeStressAllocatedBytes { get; private set; }

    public static bool RunC188()
    {
        C188FirstFailedCase = 0;
        C188FailureDetail = string.Empty;
        C188ResizeStressAllocatedBytes = 0;
        FirstFailedCase = 0;
        FailureDetail = string.Empty;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases,
            Program.GetC188GuestSelectorRoute(9u) ==
                Program.C188GuestSelectorRoute.CounterApp);
        passed &= Case(ref cases,
            Program.GetC188GuestSelectorRoute(4u) ==
                Program.C188GuestSelectorRoute.ProductionRegistry &&
            Program.GetC188GuestSelectorRoute(5u) ==
                Program.C188GuestSelectorRoute.ProductionRegistry &&
            Program.GetC188GuestSelectorRoute(6u) ==
                Program.C188GuestSelectorRoute.ProductionRegistry &&
            Program.GetC188GuestSelectorRoute(7u) ==
                Program.C188GuestSelectorRoute.ProductionRegistry &&
            Program.GetC188GuestSelectorRoute(8u) ==
                Program.C188GuestSelectorRoute.ProductionRegistry);
        bool identityStressPassed = true;
        for (int operation = 0; operation < 1000; operation++)
        {
            uint selector = (uint)(4 + (operation % 6));
            Program.C188GuestSelectorRoute expected = selector == 9u
                ? Program.C188GuestSelectorRoute.CounterApp
                : Program.C188GuestSelectorRoute.ProductionRegistry;
            identityStressPassed &=
                Program.GetC188GuestSelectorRoute(selector) == expected;
        }
        passed &= Case(ref cases, identityStressPassed);
        passed &= Case(ref cases,
            Program.GetC188GuestSelectorRoute(0u) ==
                Program.C188GuestSelectorRoute.Invalid &&
            Program.GetC188GuestSelectorRoute(10u) ==
                Program.C188GuestSelectorRoute.Invalid);

        ResetFakeHost();
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, 0u, 0u, 9u);
        int launch = Program.DispatchC188ExternalAppIdentity(&context);
        GuideXosC185ApplicationBridge bridge =
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests;
        GuideXosC185WindowAdapter window = bridge?.ServicesForTests?.Window;
        CounterApplication application =
            bridge?.ApplicationForTests as CounterApplication;
        uint generation = window?.Identity.Generation ?? 0u;

        passed &= Case(ref cases, launch == (int)GuideXosResult.Success &&
            bridge != null && application != null && window != null &&
            window.Identity.ApplicationId == CounterApplicationFactory.StableApplicationId &&
            window.Identity.Generation != 0u && window.Identity.Ordinal == 1u);
        passed &= Case(ref cases,
            s_windowRequests == 1 && s_lastWidth == 360 &&
            s_lastHeight == 200 && s_rectDraws > 0 && s_textDraws >= 2);
        passed &= Case(ref cases, application?.Counter == 0);

        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, PointerDown(250, 140)) ==
                GuideXosResult.Success && application?.Counter == 1);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation,
                KeyDown(GuideXosTextInputKey.Enter)) == GuideXosResult.Success &&
            application?.Counter == 2);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, KeyCharacter(' ')) ==
                GuideXosResult.Success && application?.Counter == 3);

        ManagedUiWindowIdentity identity = window?.Identity ?? default;
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, ResizeAction(320, 180)) ==
                GuideXosResult.Success && window?.OuterWidth == 320 &&
            window.OuterHeight == 180 && application?.Counter == 3);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, ResizeAction(720, 480)) ==
                GuideXosResult.Success && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);
        passed &= Case(ref cases,
            window?.Identity.ApplicationId == identity.ApplicationId &&
            window.Identity.Generation == identity.Generation &&
            window.Identity.Ordinal == identity.Ordinal);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, ResizeAction(721, 481)) ==
                GuideXosResult.InvalidArgument && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);

        bool invalidGeometryStressPassed = true;
        for (int index = 0; index < 250; index++)
        {
            int width = (index & 1) == 0 ? 721 : 720;
            int height = (index & 1) == 0 ? 480 : 481;
            invalidGeometryStressPassed &= InvokeGuestCandidate(9u,
                generation, ResizeAction(width, height)) ==
                GuideXosResult.InvalidArgument && window?.OuterWidth == 720 &&
                window.OuterHeight == 480;
        }
        passed &= Case(ref cases, invalidGeometryStressPassed);
        bool resizeStressPassed = true;
        long allocationStart = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < 500; index++)
        {
            int width = (index & 1) == 0 ? 320 : 720;
            int height = (index & 1) == 0 ? 180 : 480;
            resizeStressPassed &= InvokeGuestCandidate(9u, generation,
                ResizeAction(width, height)) == GuideXosResult.Success &&
                window?.OuterWidth == width && window.OuterHeight == height &&
                application?.Counter == 3;
        }
        C188ResizeStressAllocatedBytes =
            GC.GetAllocatedBytesForCurrentThread() - allocationStart;
        passed &= Case(ref cases, resizeStressPassed &&
            C188ResizeStressAllocatedBytes == 0 && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);

        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success &&
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests == null);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, PointerDown(250, 140)) ==
                GuideXosResult.InvalidArgument);

        bool staleGenerationStressPassed = true;
        for (int attempt = 0; attempt < 250; attempt++)
        {
            staleGenerationStressPassed &= InvokeGuestCandidate(9u,
                generation, PointerDown(250, 140)) == GuideXosResult.InvalidArgument;
        }
        passed &= Case(ref cases, staleGenerationStressPassed);
        context = CreateContext(&table, 0u, 0u, 9u);
        int reopened = Program.DispatchC188ExternalAppIdentity(&context);
        bridge = GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests;
        window = bridge?.ServicesForTests?.Window;
        application = bridge?.ApplicationForTests as CounterApplication;
        uint nextGeneration = window?.Identity.Generation ?? 0u;
        passed &= Case(ref cases, reopened == (int)GuideXosResult.Success &&
            nextGeneration != 0u && nextGeneration != generation &&
            application?.Counter == 0 && s_windowRequests == 2);
        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, generation, PointerDown(250, 140)) ==
                GuideXosResult.InvalidArgument && application?.Counter == 0);

        passed &= Case(ref cases,
            InvokeGuestCandidate(9u, nextGeneration,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success);
        passed &= Case(ref cases,
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests == null &&
            s_closeRequests == 2);

        bool repeatedLifecyclePassed = true;
        uint staleGeneration = nextGeneration;
        for (int cycle = 0; cycle < 100; cycle++)
        {
            context = CreateContext(&table, 0u, 0u, 9u);
            int cycleLaunch = Program.DispatchC188ExternalAppIdentity(&context);
            bridge = GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests;
            window = bridge?.ServicesForTests?.Window;
            application = bridge?.ApplicationForTests as CounterApplication;
            uint cycleGeneration = window?.Identity.Generation ?? 0u;
            repeatedLifecyclePassed &= cycleLaunch == (int)GuideXosResult.Success &&
                cycleGeneration != 0u && cycleGeneration != staleGeneration &&
                application?.Counter == 0 &&
                InvokeGuestCandidate(9u, staleGeneration, PointerDown(250, 140)) ==
                    GuideXosResult.InvalidArgument &&
                InvokeGuestCandidate(9u, cycleGeneration,
                    Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                    GuideXosResult.Success &&
                GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests == null;
            staleGeneration = cycleGeneration;
        }
        passed &= Case(ref cases, repeatedLifecyclePassed &&
            s_windowRequests == 102 && s_closeRequests == 102);

        ResetFakeHost();
        s_failNextWindow = true;
        table = CreateCallTable();
        context = CreateContext(&table, 0u, 0u, 9u);
        int failedLaunch = Program.DispatchC188ExternalAppIdentity(&context);
        passed &= Case(ref cases,
            failedLaunch != (int)GuideXosResult.Success &&
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC188Tests == null);

        if (cases != FocusedCaseCountC188)
        {
            C188FailureDetail = $"expected={FocusedCaseCountC188} actual={cases}";
            passed = false;
        }
        C188FirstFailedCase = FirstFailedCase;
        return passed;
    }

    private static GuideXosResult InvokeGuestCandidate(uint selector,
        uint generation, uint launchFlags)
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation,
            launchFlags, selector);
        return (GuideXosResult)Program.DispatchC188ExternalAppIdentity(&context);
    }
}
#endif
