#if HOSTLOGPROOF_C171_MANAGED_WINDOW_LAYOUT
using System;
using System.Runtime.InteropServices;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Creation-time Settings geometry and bounded reflow coverage for C171.</summary>
public static unsafe class GuideXosManagedWindowLayoutC171Tests
{
    public const int CaseCount = 35;
    public const int StressCaseCount = 7;
    public const int LayoutRecomputationCount = 1000;
    public const int AlternatingGeometryChangeCount = 500;
    public const int FocusTransitionCount = 500;
    public const int ScrollMovementCount = 250;
    public const int ComboCycleCount = 250;
    public const int LifecycleIterationCount = 100;

    public static int CasesExecuted { get; private set; }
    public static int FirstFailedCase { get; private set; }
    public static string MinimumAppsDetail { get; private set; } = "not-run";
    public static int StressLayoutCount { get; private set; }
    public static int StressGeometryChanges { get; private set; }
    public static int StressFocusTransitions { get; private set; }
    public static int StressScrollMovements { get; private set; }
    public static int StressComboCycles { get; private set; }
    public static int StressLifecycleIterations { get; private set; }
    public static long StressAllocationBytes { get; private set; }
    public static long StressGeneralPageAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }
    public static string StressAttachDiagnostics { get; private set; } = "not-run";
    public static string StressFailureDetail { get; private set; } = "not-run";

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        FirstFailedCase = 0;
        ManagedSettingsCenter baseline = new();
        bool initialized = baseline.InitializeForTests();
        passed &= Case(ref cases, initialized && baseline.RegistrationCount == 12 &&
            baseline.HostCapacity == 12 && baseline.View.MemberCount == 22);
        passed &= Case(ref cases, baseline.WindowWidth == 560 &&
            baseline.WindowHeight == 340);
        passed &= Case(ref cases, baseline.ClientWidth == 560);
        passed &= Case(ref cases, baseline.ClientHeight == 316);
        passed &= Case(ref cases, baseline.View.Width == 504);

        ManagedSettingsCenter minimum = new(
            ManagedSettingsCenter.MinimumWindowWidth,
            ManagedSettingsCenter.MinimumWindowHeight);
        passed &= Case(ref cases, minimum.InitializeForTests() &&
            minimum.WindowWidth == 512 && minimum.WindowHeight == 222 &&
            minimum.View.Width == 456);
        passed &= Case(ref cases, minimum.View.Height == 60 &&
            minimum.View.X >= 0 && minimum.View.Y >= 0 &&
            minimum.View.Offset <= minimum.View.MaximumOffset);

        ManagedSettingsCenter expanded = new(
            ManagedSettingsCenter.ExpandedWindowWidth,
            ManagedSettingsCenter.ExpandedWindowHeight);
        passed &= Case(ref cases, expanded.InitializeForTests() &&
            expanded.WindowWidth == 720 && expanded.WindowHeight == 420);
        passed &= Case(ref cases, expanded.View.Width == 664 &&
            expanded.View.Height == 280 && expanded.Group(0).Width == 432);

        int savedViewX = baseline.View.X;
        int savedViewY = baseline.View.Y;
        int savedViewWidth = baseline.View.Width;
        int savedViewHeight = baseline.View.Height;
        int savedButtonX = baseline.OptionsButton.X;
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(560, 340) &&
            baseline.View.X == savedViewX && baseline.View.Y == savedViewY &&
            baseline.View.Width == savedViewWidth &&
            baseline.View.Height == savedViewHeight &&
            baseline.OptionsButton.X == savedButtonX);

        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(512, 260) &&
            baseline.ClientHeight == 236 && baseline.View.Y == 118);
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(720, 420) &&
            baseline.View.Width == 664 && baseline.OptionsButton.X == 580);
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(512, 260) &&
            baseline.View.Width == 456 && baseline.OptionsButton.X == 372);

        int priorWidth = baseline.WindowWidth;
        int priorHeight = baseline.WindowHeight;
        int priorViewWidth = baseline.View.Width;
        passed &= Case(ref cases, !baseline.RecomputeLayoutForTests(-1, 260) &&
            baseline.WindowWidth == priorWidth && baseline.WindowHeight == priorHeight &&
            baseline.View.Width == priorViewWidth);
        passed &= Case(ref cases, !baseline.RecomputeLayoutForTests(721, 421) &&
            baseline.WindowWidth == priorWidth && baseline.WindowHeight == priorHeight);
        passed &= Case(ref cases, RectanglesAreNonNegative(baseline));
        passed &= Case(ref cases, baseline.OptionsButton.X >= 0 &&
            baseline.OptionsButton.X + baseline.OptionsButton.Width <= baseline.ClientWidth &&
            baseline.NavigationAppsButton.Y >= 0 &&
            baseline.NavigationAppsButton.Y + baseline.NavigationAppsButton.Height <=
                baseline.ClientHeight);
        passed &= Case(ref cases, baseline.View.X + baseline.View.Width <= baseline.ClientWidth &&
            baseline.ScrollBar.X + baseline.ScrollBar.Width <= baseline.ClientWidth);
        passed &= Case(ref cases, baseline.HeadingY >= 0 &&
            baseline.HeadingY + GuideXosLabel.TextRowHeight <= baseline.View.Y);
        passed &= Case(ref cases, baseline.FooterY >= baseline.View.Y + baseline.View.Height &&
            baseline.FooterY + GuideXosLabel.TextRowHeight <= baseline.ClientHeight);

        AssociationService minimumService = new();
        bool minimumAppsEntered = minimum.EnterDefaultAppsForTests(minimumService);
        GuideXosComboBox minimumChoice = minimum.DefaultAppsPage.Choice;
        passed &= Case(ref cases, minimumAppsEntered && minimum.View.Width == 456 &&
            minimum.View.Height == 60 && minimum.ScrollBar.Visible &&
            minimum.View.MaximumOffset > 0 && minimumChoice.Width >= 216 &&
            minimumChoice.X + minimumChoice.Width <=
                minimum.View.InnerX + minimum.View.InnerWidth);
        minimumChoice.TrySetSelectedIndex(1);
        bool minimumOffsetSet = minimum.View.SetOffset(78);
        bool minimumChoiceVisible = minimum.View.TryGetVisibleMemberBounds(
            minimumChoice, out _, out _, out int visibleChoiceWidth,
            out int visibleChoiceHeight) &&
            visibleChoiceWidth == minimumChoice.Width &&
            visibleChoiceHeight == minimumChoice.Height;
        GuideXosComboBoxResult minimumOpenResult = minimumChoice.Open();
        bool minimumPopupFits = minimumChoice.PopupY +
            minimumChoice.VisibleRowCount * GuideXosComboBox.PopupRowHeight -
                minimum.View.Offset <=
                minimum.View.InnerY + minimum.View.InnerHeight;
        minimumChoice.Close();
        bool minimumChoiceFocused = minimum.View.TryFocusMember(minimumChoice);
        bool minimumApplyFocused = minimum.View.TryMoveFocus(false) &&
            ReferenceEquals(minimum.View.FocusedMember,
                minimum.DefaultAppsPage.ApplyButton);
        bool minimumResetFocused = minimum.View.TryMoveFocus(false) &&
            ReferenceEquals(minimum.View.FocusedMember,
                minimum.DefaultAppsPage.ResetButton);
        bool minimumApplyVisible = minimum.View.TryGetVisibleMemberBounds(
            minimum.DefaultAppsPage.ApplyButton, out int minimumApplyX,
            out int minimumApplyY, out int minimumApplyWidth,
            out int minimumApplyHeight) && minimumApplyWidth > 0 &&
            minimumApplyHeight > 0;
        object minimumApplyHitMember = null;
        bool minimumApplyHitTest = minimumApplyVisible &&
            minimum.View.TryHitTest(minimumApplyX + 4,
                minimumApplyY + 4, out minimumApplyHitMember);
        object minimumApplyHitTarget = minimumApplyHitTest
            ? minimumApplyHitMember : null;
        bool minimumApplyHit = minimumApplyHitTest &&
            ReferenceEquals(minimumApplyHitTarget,
                minimum.DefaultAppsPage.ApplyButton);
        bool minimumResetVisible = minimum.View.TryGetVisibleMemberBounds(
            minimum.DefaultAppsPage.ResetButton, out int minimumResetX,
            out int minimumResetY, out int minimumResetWidth,
            out int minimumResetHeight) && minimumResetWidth > 0 &&
            minimumResetHeight > 0;
        object minimumResetHitMember = null;
        bool minimumResetHitTest = minimumResetVisible &&
            minimum.View.TryHitTest(minimumResetX + 4,
                minimumResetY + 4, out minimumResetHitMember);
        object minimumResetHitTarget = minimumResetHitTest
            ? minimumResetHitMember : null;
        bool minimumResetHit = minimumResetHitTest &&
            ReferenceEquals(minimumResetHitTarget,
                minimum.DefaultAppsPage.ResetButton);
        bool minimumPopupReachable = minimumOffsetSet && minimumChoiceVisible &&
            minimumOpenResult == GuideXosComboBoxResult.Opened && minimumPopupFits &&
            minimumChoiceFocused && minimumApplyFocused && minimumResetFocused &&
            minimumApplyHit && minimumResetHit &&
            minimum.DefaultAppsPage.HasPendingChange &&
            minimumService.MutationCount == 0;
        MinimumAppsDetail = $"offset={minimum.View.Offset} max={minimum.View.MaximumOffset} choice={minimumChoice.X},{minimumChoice.Y},{minimumChoice.Width} popupY={minimumChoice.PopupY} popupRows={minimumChoice.VisibleRowCount} view={minimum.View.InnerX},{minimum.View.InnerY},{minimum.View.InnerWidth},{minimum.View.InnerHeight} apply={minimum.DefaultAppsPage.ApplyButton.X},{minimum.DefaultAppsPage.ApplyButton.Y},{minimum.DefaultAppsPage.ApplyButton.Width} reset={minimum.DefaultAppsPage.ResetButton.X},{minimum.DefaultAppsPage.ResetButton.Y},{minimum.DefaultAppsPage.ResetButton.Width} open={minimumOpenResult} visible={minimumChoiceVisible} applyFocus={minimumApplyFocused} resetFocus={minimumResetFocused} applyHitTest={minimumApplyHitTest}:{minimumApplyHitMember?.GetType().Name} applyHit={minimumApplyHit} resetHitTest={minimumResetHitTest}:{minimumResetHitMember?.GetType().Name} resetHit={minimumResetHit}";
        passed &= Case(ref cases, minimumPopupReachable);

        AssociationService service = new();
        bool enteredApps = baseline.EnterDefaultAppsForTests(service);
        GuideXosComboBox choice = baseline.DefaultAppsPage.Choice;
        passed &= Case(ref cases, enteredApps && choice.Width >= 216 &&
            choice.X >= baseline.View.InnerX &&
            choice.X + choice.Width <= baseline.View.InnerX + baseline.View.InnerWidth);
        passed &= Case(ref cases, InsideView(baseline.View,
            baseline.DefaultAppsPage.ApplyButton));
        passed &= Case(ref cases, InsideView(baseline.View,
            baseline.DefaultAppsPage.ResetButton));
        passed &= Case(ref cases, baseline.View.ContentExtent > 0 &&
            baseline.View.VisibleExtent == baseline.View.InnerHeight &&
            baseline.View.MaximumOffset == Math.Max(0,
                baseline.View.ContentExtent - baseline.View.VisibleExtent));

        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(512, 260) &&
            baseline.ScrollBar.Visible && baseline.View.MaximumOffset > 0);
        baseline.View.SetOffset(baseline.View.MaximumOffset);
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(720, 420) &&
            !baseline.ScrollBar.Visible && baseline.View.Offset == 0 &&
            baseline.View.Offset <= baseline.View.MaximumOffset);

        choice.TrySetSelectedIndex(1);
        bool hostFocused = baseline.ControlHost.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            baseline.View.TryFocusMember(choice);
        int focusedId = baseline.ControlHost.ActiveControlId;
        object focusedMember = baseline.View.FocusedMember;
        passed &= Case(ref cases, hostFocused && focusedId == 1 &&
            ReferenceEquals(focusedMember, choice));
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(512, 260) &&
            baseline.ControlHost.ActiveControlId == focusedId &&
            ReferenceEquals(baseline.View.FocusedMember, focusedMember));
        passed &= Case(ref cases, baseline.View.TryHitTest(choice.X + 4,
            choice.Y + 4, out object hit) && ReferenceEquals(hit, choice));

        bool opened = choice.Open() == GuideXosComboBoxResult.Opened &&
            baseline.ControlHost.TryAcquireTransientInputCapture(12);
        bool pendingBefore = baseline.DefaultAppsPage.HasPendingChange &&
            baseline.DefaultAppsPage.PendingChoice == 1;
        int mutationCount = service.MutationCount;
        passed &= Case(ref cases, opened && pendingBefore);
        passed &= Case(ref cases, baseline.RecomputeLayoutForTests(720, 420) &&
            !choice.IsOpen && !baseline.ControlHost.HasTransientInputCapture &&
            baseline.DefaultAppsPage.HasPendingChange &&
            baseline.DefaultAppsPage.PendingChoice == 1 &&
            service.MutationCount == mutationCount);

        passed &= Case(ref cases, GxAbi.HostAbiVersion == 4u &&
            GxAbi.HostCallTableSize == 128u &&
            GxAbi.ApplicationSnapshotOffset == 104u &&
            GxAbi.ApplicationCloseOffset == 112u &&
            GxAbi.AssociationServiceOffset == 120u &&
            sizeof(NativeHostCallTable) == 128 &&
            Marshal.OffsetOf<NativeHostCallTable>("associationService").ToInt32() == 120 &&
            sizeof(NativeAssociationServiceResponse) == 328 &&
            ManagedSettingsStore.FormatVersion == 2 &&
            ManagedSettingsStore.EncodedFileBytes == 26);

        passed &= Case(ref cases, baseline.RegistrationCount == 12 &&
            baseline.HostCapacity == 12 && baseline.View.MemberCount == 8 &&
            service.QueryCount >= 1 && service.MutationCount == 0);

        CasesExecuted = cases;
        bool result = passed && cases == CaseCount;
        host?.TryLog(result
            ? "C171-WINDOW-LAYOUT cases=35 path=creation-time result=PASS"u8
            : "C171-WINDOW-LAYOUT result=FAIL"u8);
        return result;
    }

    public static bool RunStress(GuideXosHost host)
    {
        AssociationService service = new();
        ManagedSettingsCenter app = new();
        bool ready = app.InitializeForTests() &&
            app.EnterDefaultAppsForTests(service);
        app.RecomputeLayoutForTests(720, 420);
        app.RecomputeLayoutForTests(512, 260);

        _ = GC.GetAllocatedBytesForCurrentThread();
        long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        bool layouts = true;
        int alternatingChanges = 0;
        for (int index = 0; index < LayoutRecomputationCount; index++)
        {
            int width;
            int height;
            if (index < AlternatingGeometryChangeCount)
            {
                bool useExpanded = (index & 1) != 0;
                width = useExpanded ? ManagedSettingsCenter.ExpandedWindowWidth
                    : ManagedSettingsCenter.CompactWindowWidth;
                height = useExpanded ? ManagedSettingsCenter.ExpandedWindowHeight
                    : ManagedSettingsCenter.CompactWindowHeight;
                ++alternatingChanges;
            }
            else
            {
                width = ManagedSettingsCenter.CompactWindowWidth;
                height = ManagedSettingsCenter.CompactWindowHeight;
            }
            layouts &= app.RecomputeLayoutForTests(width, height);
        }
        long allocatedAfter = GC.GetAllocatedBytesForCurrentThread();
        StressAllocationBytes = allocatedAfter - allocatedBefore;
        StressAttachDiagnostics = app.DefaultAppsPage.AttachDiagnostics;

        ManagedSettingsCenter generalPage = new();
        bool generalReady = generalPage.InitializeForTests();
        for (int index = 0; index < 8; index++)
            generalReady &= generalPage.RecomputeLayoutForTests(
                (index & 1) == 0 ? ManagedSettingsCenter.CompactWindowWidth
                    : ManagedSettingsCenter.ExpandedWindowWidth,
                (index & 1) == 0 ? ManagedSettingsCenter.CompactWindowHeight
                    : ManagedSettingsCenter.ExpandedWindowHeight);
        long generalAllocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < 8; index++)
            generalReady &= generalPage.RecomputeLayoutForTests(
                (index & 1) == 0 ? ManagedSettingsCenter.CompactWindowWidth
                    : ManagedSettingsCenter.ExpandedWindowWidth,
                (index & 1) == 0 ? ManagedSettingsCenter.CompactWindowHeight
                    : ManagedSettingsCenter.ExpandedWindowHeight);
        StressGeneralPageAllocationBytes =
            GC.GetAllocatedBytesForCurrentThread() - generalAllocatedBefore;

        bool focus = ready &&
            app.ControlHost.TryFocus(11) == GuideXosControlHostResult.Focused;
        for (int index = 0; index < FocusTransitionCount; index++)
        {
            int target = (index & 1) == 0 ? 7 : 11;
            focus &= app.ControlHost.TryFocus(target) ==
                GuideXosControlHostResult.Focused;
        }

        bool scrolling = true;
        for (int index = 0; index < ScrollMovementCount; index++)
        {
            int delta = (index & 1) == 0 ? 1 : -1;
            int before = app.View.Offset;
            scrolling &= app.View.ScrollSmall(delta) &&
                app.View.Offset != before;
        }

        GuideXosComboBox combo = app.DefaultAppsPage.Choice;
        bool combos = app.View.TryFocusMember(combo) &&
            ReferenceEquals(app.View.FocusedMember, combo);
        for (int index = 0; index < ComboCycleCount; index++)
        {
            combos &= combo.Open() == GuideXosComboBoxResult.Opened &&
                app.ControlHost.TryAcquireTransientInputCapture(12);
            combos &= combo.Close() == GuideXosComboBoxResult.Closed;
            app.ControlHost.RefreshVisibility();
            combos &= !combo.IsOpen && !app.ControlHost.HasTransientInputCapture;
        }

        bool lifecycle = true;
        ManagedSettingsCenter latest = app;
        for (int index = 0; index < LifecycleIterationCount; index++)
        {
            latest = new ManagedSettingsCenter();
            lifecycle &= latest.InitializeForTests() &&
                latest.RecomputeLayoutForTests(
                    ManagedSettingsCenter.CompactWindowWidth,
                    ManagedSettingsCenter.CompactWindowHeight) &&
                latest.RegistrationCount == 12 && latest.View.MemberCount == 22;
        }

        int expectedWidth = app.View.Width;
        int expectedHeight = app.View.Height;
        object focusedMember = app.View.FocusedMember;
        int expectedRegistrations = app.RegistrationCount;
        bool stable = app.RecomputeLayoutForTests(
                ManagedSettingsCenter.CompactWindowWidth,
                ManagedSettingsCenter.CompactWindowHeight) &&
            app.View.Width == expectedWidth && app.View.Height == expectedHeight &&
            app.RegistrationCount == expectedRegistrations &&
            app.View.MemberCount == 8 && !combo.IsOpen &&
            !app.ControlHost.HasTransientInputCapture &&
            ReferenceEquals(app.View.FocusedMember, focusedMember);
        StressDriftCount = stable ? 0 : 1;
        bool result = ready && layouts && alternatingChanges ==
                AlternatingGeometryChangeCount && focus && scrolling && combos &&
            lifecycle && stable && StressAllocationBytes == 0 &&
            generalReady && StressGeneralPageAllocationBytes == 0;
        StressFailureDetail = $"ready={ready} layouts={layouts} focus={focus} scroll={scrolling} combo={combos} lifecycle={lifecycle} stable={stable} generalReady={generalReady}";

        StressLayoutCount = LayoutRecomputationCount;
        StressGeometryChanges = alternatingChanges;
        StressFocusTransitions = FocusTransitionCount;
        StressScrollMovements = ScrollMovementCount;
        StressComboCycles = ComboCycleCount;
        StressLifecycleIterations = LifecycleIterationCount;
        host?.TryLog(result
            ? "C171-STRESS layout=1000 geometry=500 focus=500 scroll=250 combo=250 lifecycle=100 allocationBytes=0 drift=0 result=PASS"u8
            : "C171-STRESS result=FAIL"u8);
        return result;
    }

    private static bool RectanglesAreNonNegative(ManagedSettingsCenter app)
    {
        if (app.View.X < 0 || app.View.Y < 0 || app.ScrollBar.X < 0 ||
            app.ScrollBar.Y < 0 || app.OptionsButton.X < 0 ||
            app.NavigationAppsButton.X < 0)
            return false;
        for (int index = 0; index < app.SectionGroupCount; index++)
        {
            GuideXosGroupBox group = app.Group(index);
            if (group.X < 0 || group.Y < 0 || group.Width <= 0 ||
                group.Height <= 0) return false;
        }
        return true;
    }

    private static bool InsideView(GuideXosScrollView view, GuideXosButton button) =>
        button.X >= view.InnerX && button.Y >= view.InnerY &&
        button.X + button.Width <= view.InnerX + view.InnerWidth &&
        button.Y + button.Height <= view.InnerY + view.InnerHeight ||
        button.X >= view.InnerX && button.Y >= view.InnerY &&
        button.X + button.Width <= view.InnerX + view.InnerWidth &&
        button.Y + button.Height <= view.InnerY + view.ContentExtent;

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
        public int QueryCount { get; private set; }
        public int MutationCount { get; private set; }

        public GuideXosAssociationResult QueryTxt()
        {
            ++QueryCount;
            return MakeResult(GuideXosAssociationStatus.Success, State);
        }

        public GuideXosAssociationResult SetManagedNotes() => Mutate(
            GuideXosAssociationOverrideState.ApplicationOverride);
        public GuideXosAssociationResult DisableTxt() => Mutate(
            GuideXosAssociationOverrideState.Disabled);
        public GuideXosAssociationResult ResetTxt() => Mutate(
            GuideXosAssociationOverrideState.NoOverride);

        private GuideXosAssociationResult Mutate(
            GuideXosAssociationOverrideState next)
        {
            ++MutationCount;
            State = next;
            return MakeResult(GuideXosAssociationStatus.Success, State);
        }

        private static GuideXosAssociationResult MakeResult(
            GuideXosAssociationStatus status,
            GuideXosAssociationOverrideState state)
        {
            NativeAssociationServiceResponse response = default;
            NativeAssociationServiceResponse* pointer = &response;
            pointer->status = (uint)status;
            pointer->overrideState = (uint)state;
            pointer->hasEffectiveAssociation = state ==
                GuideXosAssociationOverrideState.Disabled ? 0u : 1u;
            byte* compiled = pointer->compiledDefaultAppId;
            "com.guidexos.apps.managed.notes"u8.CopyTo(new Span<byte>(compiled, 96));
            byte* normalized = pointer->normalizedExtension;
            ".txt"u8.CopyTo(new Span<byte>(normalized, 16));
            if (state == GuideXosAssociationOverrideState.NoOverride)
            {
                byte* effective = pointer->effectiveAppId;
                "com.guidexos.apps.managed.notes"u8.CopyTo(
                    new Span<byte>(effective, 96));
            }
            else if (state == GuideXosAssociationOverrideState.ApplicationOverride)
            {
                byte* overridden = pointer->overrideAppId;
                "com.guidexos.apps.managed.notes"u8.CopyTo(
                    new Span<byte>(overridden, 96));
                byte* effective = pointer->effectiveAppId;
                "com.guidexos.apps.managed.notes"u8.CopyTo(
                    new Span<byte>(effective, 96));
            }
            return new GuideXosAssociationResult(status, response);
        }
    }
}
#endif
