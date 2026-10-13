#if HOSTLOGPROOF_C172_LIVE_WINDOW_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static unsafe class GuideXosManagedWindowResizeC172Tests
{
    public const int CaseCount = 43;
    public const int StressCaseCount = 7;
    public const int RuntimeGeometryUpdateCount = 1000;
    public const int AlternatingGeometryTransitionCount = 500;
    public const int FocusTransitionCount = 500;
    public const int ComboCycleCount = 250;
    public const int ScrollMovementCount = 250;
    public const int LifecycleIterationCount = 100;

    public static int CasesExecuted { get; private set; }
    public static int FirstFailedCase { get; private set; }
    public static int StressGeometryUpdates { get; private set; }
    public static int StressAlternatingTransitions { get; private set; }
    public static int StressFocusTransitions { get; private set; }
    public static int StressComboCycles { get; private set; }
    public static int StressScrollMovements { get; private set; }
    public static int StressLifecycleIterations { get; private set; }
    public static long StressAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }
    public static string StressFailureDetail { get; private set; } = "not-run";

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        FirstFailedCase = 0;
        ManagedSettingsCenter app = new();
        bool ready = app.InitializeForTests();
        passed &= Case(ref cases, ready && app.WindowWidth == 560 &&
            app.WindowHeight == 340);
        passed &= Case(ref cases, app.ClientWidth == 560 &&
            app.ClientHeight == 316);
        passed &= Case(ref cases, LayoutWithinClient(app));

        bool encoded = GuideXosManagedWindowGeometryC172.TryEncodeAction(
            720, 420, out uint actionId);
        passed &= Case(ref cases, encoded);
        passed &= Case(ref cases,
            (actionId & GxAbi.LaunchFlagInput) == 0u);
        passed &= Case(ref cases,
            GuideXosManagedWindowGeometryC172.TryDecodeAction(
                actionId, out int decodedWidth, out int decodedHeight) &&
            decodedWidth == 720 && decodedHeight == 420);
        passed &= Case(ref cases,
            !GuideXosManagedWindowGeometryC172.TryEncodeAction(
                4096, 420, out _));
        passed &= Case(ref cases,
            !GuideXosManagedWindowGeometryC172.TryDecodeAction(
                0x01000000u, out _, out _));

        passed &= Case(ref cases,
            app.ApplyLiveGeometryForTests(720, 420));
        passed &= Case(ref cases,
            app.WindowWidth == 720 && app.WindowHeight == 420);
        passed &= Case(ref cases,
            app.ClientWidth == 720 && app.ClientHeight == 396);
        passed &= Case(ref cases, LayoutWithinClient(app));
        passed &= Case(ref cases,
            app.View.ContentExtent > 0 &&
            app.View.Offset <= app.View.MaximumOffset);

        passed &= Case(ref cases,
            app.ApplyLiveGeometryForTests(512, 222));
        passed &= Case(ref cases,
            app.WindowWidth == 512 && app.WindowHeight == 222);
        passed &= Case(ref cases,
            app.ClientWidth == 512 && app.ClientHeight == 198);
        passed &= Case(ref cases,
            app.View.Height == 60 && LayoutWithinClient(app));
        passed &= Case(ref cases,
            app.View.VisibleExtent == app.View.InnerHeight &&
            app.View.MaximumOffset >= 0 &&
            app.View.Offset <= app.View.MaximumOffset);

        int priorWidth = app.WindowWidth;
        int priorHeight = app.WindowHeight;
        passed &= Case(ref cases,
            !app.ApplyLiveGeometryForTests(511, 222));
        passed &= Case(ref cases,
            !app.ApplyLiveGeometryForTests(512, 221));
        passed &= Case(ref cases,
            !app.ApplyLiveGeometryForTests(721, 420));
        passed &= Case(ref cases,
            !app.ApplyLiveGeometryForTests(720, 421));
        passed &= Case(ref cases, app.WindowWidth == priorWidth &&
            app.WindowHeight == priorHeight);
        passed &= Case(ref cases,
            app.ApplyLiveGeometryForTests(priorWidth, priorHeight));

        for (int index = 0; index < 128; index++)
            passed &= app.ApplyLiveGeometryForTests(priorWidth, priorHeight);
        passed &= Case(ref cases,
            app.WindowWidth == 512 && app.WindowHeight == 222 &&
            LayoutWithinClient(app));

        passed &= app.ApplyLiveGeometryForTests(560, 340);
        AssociationService service = new();
        bool appsPage = app.EnterDefaultAppsForTests(service);
        passed &= Case(ref cases, appsPage && app.IsDefaultAppsPage);
        passed &= Case(ref cases, service.MutationCount == 0);
        GuideXosComboBox choice = app.DefaultAppsPage.Choice;
        choice.TrySetSelectedIndex(1);
        passed &= Case(ref cases,
            app.DefaultAppsPage.HasPendingChange &&
            app.DefaultAppsPage.PendingChoice == 1);
        bool focused = app.ControlHost.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            app.View.TryFocusMember(choice);
        object focusedMember = app.View.FocusedMember;
        passed &= Case(ref cases, focused &&
            app.ControlHost.ActiveControlId == 1 &&
            ReferenceEquals(focusedMember, choice));
        passed &= Case(ref cases,
            app.View.TryHitTest(choice.X + 4, choice.Y + 4,
                out object hit) && ReferenceEquals(hit, choice));
        passed &= app.ApplyLiveGeometryForTests(512, 260);
        passed &= Case(ref cases,
            app.ScrollBar.Visible && app.View.MaximumOffset > 0);

        app.View.SetOffset(app.View.MaximumOffset);
        passed &= Case(ref cases,
            app.View.Offset == app.View.MaximumOffset &&
            app.View.Offset <= app.View.MaximumOffset);
        bool popupOpened = choice.Open() == GuideXosComboBoxResult.Opened &&
            app.ControlHost.TryAcquireTransientInputCapture(12);
        passed &= Case(ref cases, popupOpened && app.HasCapture);
        int mutationCount = service.MutationCount;
        passed &= Case(ref cases,
            app.ApplyLiveGeometryForTests(720, 420) &&
            !choice.IsOpen && !app.HasCapture && !app.HasDragOwner);
        passed &= Case(ref cases,
            app.DefaultAppsPage.HasPendingChange &&
            app.DefaultAppsPage.PendingChoice == 1);
        passed &= Case(ref cases, service.MutationCount == mutationCount);
        passed &= Case(ref cases,
            app.ControlHost.ActiveControlId == 1 &&
            ReferenceEquals(app.View.FocusedMember, focusedMember));
        passed &= Case(ref cases,
            app.View.TryHitTest(choice.X + 4, choice.Y + 4,
                out hit) && ReferenceEquals(hit, choice));
        passed &= Case(ref cases,
            !app.ScrollBar.Visible && app.View.Offset == 0 &&
            app.View.Offset <= app.View.MaximumOffset);

        object memberBeforeTab = app.View.FocusedMember;
        GuideXosControlHostResult tab = app.ControlHost.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        passed &= Case(ref cases, tab != GuideXosControlHostResult.Ignored &&
            !ReferenceEquals(app.View.FocusedMember, memberBeforeTab));
        GuideXosControlHostResult shiftTab = app.ControlHost.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, shift: true));
        passed &= Case(ref cases, shiftTab != GuideXosControlHostResult.Ignored &&
            ReferenceEquals(app.View.FocusedMember, memberBeforeTab));
        passed &= Case(ref cases,
            app.RegistrationCount == 12 && app.HostCapacity == 12 &&
            app.View.MemberCount == 8 && service.MutationCount == 0);
        passed &= Case(ref cases,
            app.WindowWidth == 720 && app.WindowHeight == 420 &&
            LayoutWithinClient(app));

        CasesExecuted = cases;
        bool result = passed && cases == CaseCount;
        host?.TryLog(result
            ? "C172-WINDOW-RESIZE cases=43 path=managed-action result=PASS"u8
            : "C172-WINDOW-RESIZE result=FAIL"u8);
        return result;
    }

    public static bool RunStress(GuideXosHost host)
    {
        ManagedSettingsCenter app = new();
        AssociationService service = new();
        bool ready = app.InitializeForTests() &&
            app.EnterDefaultAppsForTests(service);
        int initialViewX = app.View.X;
        int initialViewY = app.View.Y;
        int initialViewWidth = app.View.Width;
        int initialViewHeight = app.View.Height;
        int initialRegistrationCount = app.RegistrationCount;

        _ = GC.GetAllocatedBytesForCurrentThread();
        long allocationBefore = GC.GetAllocatedBytesForCurrentThread();
        bool layouts = ready;
        for (int index = 0; index < RuntimeGeometryUpdateCount; index++)
        {
            bool compact = (index & 1) == 0;
            layouts &= app.ApplyLiveGeometryForTests(compact ? 512 : 720,
                compact ? 260 : 420);
        }
        layouts &= app.ApplyLiveGeometryForTests(560, 340);
        StressAllocationBytes =
            GC.GetAllocatedBytesForCurrentThread() - allocationBefore;
        StressGeometryUpdates = RuntimeGeometryUpdateCount;
        StressAlternatingTransitions =
            RuntimeGeometryUpdateCount / 2;

        bool focus = ready && app.ControlHost.TryFocus(7) ==
            GuideXosControlHostResult.Focused;
        for (int index = 0; index < FocusTransitionCount; index++)
        {
            int target = (index & 1) == 0 ? 11 : 7;
            focus &= app.ControlHost.TryFocus(target) ==
                GuideXosControlHostResult.Focused;
        }
        StressFocusTransitions = FocusTransitionCount;

        GuideXosComboBox combo = app.DefaultAppsPage.Choice;
        bool combos = app.View.TryFocusMember(combo);
        for (int index = 0; index < ComboCycleCount; index++)
        {
            combos &= combo.Open() == GuideXosComboBoxResult.Opened &&
                app.ControlHost.TryAcquireTransientInputCapture(12);
            combos &= app.ApplyLiveGeometryForTests(
                (index & 1) == 0 ? 720 : 512,
                (index & 1) == 0 ? 420 : 260);
            combos &= !combo.IsOpen && !app.HasCapture && !app.HasDragOwner;
        }
        StressComboCycles = ComboCycleCount;

        bool scrolling = app.ApplyLiveGeometryForTests(512, 260) &&
            app.View.MaximumOffset > 0;
        for (int index = 0; index < ScrollMovementCount; index++)
        {
            int delta = (index & 1) == 0 ? 1 : -1;
            int priorOffset = app.View.Offset;
            scrolling &= app.View.ScrollSmall(delta) &&
                app.View.Offset != priorOffset;
        }
        StressScrollMovements = ScrollMovementCount;

        bool lifecycles = true;
        for (int index = 0; index < LifecycleIterationCount; index++)
        {
            ManagedSettingsCenter iteration = new();
            lifecycles &= iteration.InitializeForTests() &&
                iteration.ApplyLiveGeometryForTests(720, 420) &&
                iteration.ApplyLiveGeometryForTests(512, 260) &&
                iteration.RegistrationCount == 12;
        }
        StressLifecycleIterations = LifecycleIterationCount;

        bool restored = app.ApplyLiveGeometryForTests(560, 340);
        StressDriftCount = restored && app.View.X == initialViewX &&
            app.View.Y == initialViewY && app.View.Width == initialViewWidth &&
            app.View.Height == initialViewHeight &&
            app.RegistrationCount == initialRegistrationCount &&
            service.MutationCount == 0 ? 0 : 1;
        bool result = layouts && focus && combos && scrolling && lifecycles &&
            StressAllocationBytes <= 4096 && StressDriftCount == 0 &&
            app.RegistrationCount == 12 && app.View.MemberCount == 8 &&
            !app.HasCapture && !app.HasDragOwner;
        StressFailureDetail = result ? "none" :
            $"layout={layouts} focus={focus} combo={combos} scroll={scrolling} lifecycle={lifecycles} allocation={StressAllocationBytes} drift={StressDriftCount}";
        host?.TryLog(result
            ? "C172-STRESS result=PASS"u8
            : "C172-STRESS result=FAIL"u8);
        return result;
    }

    private static bool LayoutWithinClient(ManagedSettingsCenter app) =>
        app.View.X >= 0 && app.View.Y >= 0 && app.View.Width >= 0 &&
        app.View.Height >= 0 && app.View.X + app.View.Width <= app.ClientWidth &&
        app.View.Y + app.View.Height <= app.ClientHeight &&
        app.View.MaximumOffset >= 0 && app.View.Offset >= 0 &&
        app.View.Offset <= app.View.MaximumOffset;

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        if (!passed && FirstFailedCase == 0) FirstFailedCase = count;
        return passed;
    }

    private sealed class AssociationService : IManagedDefaultAppsAssociationService
    {
        public GuideXosAssociationOverrideState State { get; private set; } =
            GuideXosAssociationOverrideState.NoOverride;
        public int MutationCount { get; private set; }

        public GuideXosAssociationResult QueryTxt() => Result(State);
        public GuideXosAssociationResult SetManagedNotes() => Mutate(
            GuideXosAssociationOverrideState.ApplicationOverride);
        public GuideXosAssociationResult DisableTxt() => Mutate(
            GuideXosAssociationOverrideState.Disabled);
        public GuideXosAssociationResult ResetTxt() => Mutate(
            GuideXosAssociationOverrideState.NoOverride);

        private GuideXosAssociationResult Mutate(
            GuideXosAssociationOverrideState state)
        {
            ++MutationCount;
            State = state;
            return Result(state);
        }

        private static GuideXosAssociationResult Result(
            GuideXosAssociationOverrideState state)
        {
            NativeAssociationServiceResponse response = default;
            NativeAssociationServiceResponse* pointer = &response;
            pointer->status = (uint)GuideXosAssociationStatus.Success;
            pointer->overrideState = (uint)state;
            pointer->hasEffectiveAssociation = state ==
                GuideXosAssociationOverrideState.Disabled ? 0u : 1u;
            byte* compiled = pointer->compiledDefaultAppId;
            "com.guidexos.apps.managed.notes"u8.CopyTo(
                new Span<byte>(compiled, 96));
            byte* normalized = pointer->normalizedExtension;
            ".txt"u8.CopyTo(new Span<byte>(normalized, 16));
            if (state != GuideXosAssociationOverrideState.Disabled)
            {
                byte* effective = pointer->effectiveAppId;
                "com.guidexos.apps.managed.notes"u8.CopyTo(
                    new Span<byte>(effective, 96));
            }
            return new GuideXosAssociationResult(
                GuideXosAssociationStatus.Success, response);
        }
    }
}
#endif
