#if HOSTLOGPROOF_C187_GUEST_CANDIDATE
using System;
using GuideXos.C185.CounterApp;
using GuideXos.ManagedUi;

namespace HostLogProof;

public static unsafe partial class GuideXosManagedHostServicesC185Tests
{
    public const int FocusedCaseCountC187 = 22;
    public static int C187FirstFailedCase { get; private set; }
    public static string C187FailureDetail { get; private set; } = string.Empty;
    public static long C187ResizeStressAllocatedBytes { get; private set; }

    public static bool RunC187()
    {
        C187FirstFailedCase = 0;
        C187FailureDetail = string.Empty;
        C187ResizeStressAllocatedBytes = 0;
        FirstFailedCase = 0;
        FailureDetail = string.Empty;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases,
            Program.GetC187GuestSelectorRoute(4u) ==
                Program.C187GuestSelectorRoute.CounterApp);
        passed &= Case(ref cases,
            Program.GetC187GuestSelectorRoute(5u) ==
                Program.C187GuestSelectorRoute.ProductionRegistry &&
            Program.GetC187GuestSelectorRoute(6u) ==
                Program.C187GuestSelectorRoute.ProductionRegistry &&
            Program.GetC187GuestSelectorRoute(7u) ==
                Program.C187GuestSelectorRoute.ProductionRegistry &&
            Program.GetC187GuestSelectorRoute(8u) ==
                Program.C187GuestSelectorRoute.ProductionRegistry);
        passed &= Case(ref cases,
            Program.GetC187GuestSelectorRoute(0u) ==
                Program.C187GuestSelectorRoute.Invalid &&
            Program.GetC187GuestSelectorRoute(9u) ==
                Program.C187GuestSelectorRoute.Invalid);

        ResetFakeHost();
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, 0u, 0u, 4u);
        int launch = Program.DispatchC187GuestCandidate(&context);
        GuideXosC185ApplicationBridge bridge =
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests;
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
            InvokeGuestCandidate(4u, generation, PointerDown(250, 140)) ==
                GuideXosResult.Success && application?.Counter == 1);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation,
                KeyDown(GuideXosTextInputKey.Enter)) == GuideXosResult.Success &&
            application?.Counter == 2);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, KeyCharacter(' ')) ==
                GuideXosResult.Success && application?.Counter == 3);

        ManagedUiWindowIdentity identity = window?.Identity ?? default;
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, ResizeAction(320, 180)) ==
                GuideXosResult.Success && window?.OuterWidth == 320 &&
            window.OuterHeight == 180 && application?.Counter == 3);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, ResizeAction(720, 480)) ==
                GuideXosResult.Success && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);
        passed &= Case(ref cases,
            window?.Identity.ApplicationId == identity.ApplicationId &&
            window.Identity.Generation == identity.Generation &&
            window.Identity.Ordinal == identity.Ordinal);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, ResizeAction(721, 481)) ==
                GuideXosResult.InvalidArgument && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);

        bool resizeStressPassed = true;
        long allocationStart = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < 100; index++)
        {
            int width = (index & 1) == 0 ? 320 : 720;
            int height = (index & 1) == 0 ? 180 : 480;
            resizeStressPassed &= InvokeGuestCandidate(4u, generation,
                ResizeAction(width, height)) == GuideXosResult.Success &&
                window?.OuterWidth == width && window.OuterHeight == height &&
                application?.Counter == 3;
        }
        C187ResizeStressAllocatedBytes =
            GC.GetAllocatedBytesForCurrentThread() - allocationStart;
        passed &= Case(ref cases, resizeStressPassed &&
            C187ResizeStressAllocatedBytes == 0 && window?.OuterWidth == 720 &&
            window.OuterHeight == 480 && application?.Counter == 3);

        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success &&
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests == null);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, PointerDown(250, 140)) ==
                GuideXosResult.InvalidArgument);

        context = CreateContext(&table, 0u, 0u, 4u);
        int reopened = Program.DispatchC187GuestCandidate(&context);
        bridge = GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests;
        window = bridge?.ServicesForTests?.Window;
        application = bridge?.ApplicationForTests as CounterApplication;
        uint nextGeneration = window?.Identity.Generation ?? 0u;
        passed &= Case(ref cases, reopened == (int)GuideXosResult.Success &&
            nextGeneration != 0u && nextGeneration != generation &&
            application?.Counter == 0 && s_windowRequests == 2);
        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, generation, PointerDown(250, 140)) ==
                GuideXosResult.InvalidArgument && application?.Counter == 0);

        passed &= Case(ref cases,
            InvokeGuestCandidate(4u, nextGeneration,
                Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                GuideXosResult.Success);
        passed &= Case(ref cases,
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests == null &&
            s_closeRequests == 2);

        bool repeatedLifecyclePassed = true;
        uint staleGeneration = nextGeneration;
        for (int cycle = 0; cycle < 3; cycle++)
        {
            context = CreateContext(&table, 0u, 0u, 4u);
            int cycleLaunch = Program.DispatchC187GuestCandidate(&context);
            bridge = GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests;
            window = bridge?.ServicesForTests?.Window;
            application = bridge?.ApplicationForTests as CounterApplication;
            uint cycleGeneration = window?.Identity.Generation ?? 0u;
            repeatedLifecyclePassed &= cycleLaunch == (int)GuideXosResult.Success &&
                cycleGeneration != 0u && cycleGeneration != staleGeneration &&
                application?.Counter == 0 &&
                InvokeGuestCandidate(4u, staleGeneration, PointerDown(250, 140)) ==
                    GuideXosResult.InvalidArgument &&
                InvokeGuestCandidate(4u, cycleGeneration,
                    Action(GuideXosApplicationRegistry.LifetimeCloseActionId)) ==
                    GuideXosResult.Success &&
                GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests == null;
            staleGeneration = cycleGeneration;
        }
        passed &= Case(ref cases, repeatedLifecyclePassed &&
            s_windowRequests == 5 && s_closeRequests == 5);

        ResetFakeHost();
        s_failNextWindow = true;
        table = CreateCallTable();
        context = CreateContext(&table, 0u, 0u, 4u);
        int failedLaunch = Program.DispatchC187GuestCandidate(&context);
        passed &= Case(ref cases,
            failedLaunch != (int)GuideXosResult.Success &&
            GuideXosManagedHostServicesC185Composition.CurrentBridgeForC187Tests == null);

        if (cases != FocusedCaseCountC187)
        {
            C187FailureDetail = $"expected={FocusedCaseCountC187} actual={cases}";
            passed = false;
        }
        C187FirstFailedCase = FirstFailedCase;
        return passed;
    }

    private static GuideXosResult InvokeGuestCandidate(uint selector,
        uint generation, uint launchFlags)
    {
        NativeHostCallTable table = CreateCallTable();
        NativeGxAppContext context = CreateContext(&table, generation,
            launchFlags, selector);
        return (GuideXosResult)Program.DispatchC187GuestCandidate(&context);
    }
}
#endif
