#if HOSTLOGPROOF_C179_MANAGED_TASK_MANAGER_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static unsafe class GuideXosManagedTaskManagerResizeC179Tests
{
    public const int CaseCount = 20;
    public const int StressCaseCount = 5;
    public const int RequiredLayoutUpdates = 1000;
    public const int RequiredAlternatingResizes = 500;
    public const int RequiredRefreshResizeCycles = 250;
    public const int RequiredRejectedGeometryChecks = 250;
    public const int RequiredLifecycleCycles = 100;

    public static int LayoutUpdates { get; private set; }
    public static int AlternatingResizes { get; private set; }
    public static int RefreshResizeCycles { get; private set; }
    public static int RejectedGeometryChecks { get; private set; }
    public static int LifecycleCycles { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }
    public static long RefreshAllocationBytes { get; private set; }
    public static int GeometryDriftCount { get; private set; }
    public static string FirstFailedCase { get; private set; } = string.Empty;
    public static string RefreshDetail { get; private set; } = string.Empty;

    public static bool Run(GuideXosHost host)
    {
        FirstFailedCase = string.Empty;
        int cases = 0;
        bool passed = true;
        ManagedTaskManagerC161 app = new();
        passed &= Case(ref cases, "initial-geometry",
            app.InitializeForC179Tests() &&
            app.WindowWidth == ManagedTaskManagerC161.SurfaceWidth &&
            app.WindowHeight == ManagedTaskManagerC161.SurfaceHeight &&
            app.Controller.ClientWidth == 800 &&
            app.Controller.ClientHeight == 346 &&
            app.Controller.List.RenderWidth == 45 &&
            app.Controller.List.VisibleRowCount == 10 &&
            app.Controller.ButtonRowY == 276 && app.ControlCount == 4);

        bool minimum = app.TryApplyResizeForTests(
            ManagedTaskManagerC161.MinimumWindowWidthC179,
            ManagedTaskManagerC161.MinimumWindowHeightC179);
        passed &= Case(ref cases, "minimum-layout-usable",
            minimum && app.Controller.ClientWidth == 800 &&
            app.Controller.ClientHeight == 320 &&
            app.Controller.List.Width == 360 &&
            app.Controller.List.VisibleRowCount == 10 &&
            app.Controller.ButtonRowY + app.Controller.RefreshButton.Height <=
                app.Controller.ClientHeight - 16 &&
            DetailsFit(app.Controller));

        bool maximum = app.TryApplyResizeForTests(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        passed &= Case(ref cases, "maximum-layout-expands-table",
            maximum && app.Controller.ClientWidth == 960 &&
            app.Controller.ClientHeight == 428 &&
            app.Controller.List.RenderWidth == 48 &&
            app.Controller.List.VisibleRowCount == 16 &&
            app.Controller.DetailsX == 424 &&
            app.Controller.StatusY == 356 &&
            app.Controller.ButtonRowY == 384 &&
            app.Controller.ButtonRowY + app.Controller.CloseButton.Height == 412 &&
            DetailsFit(app.Controller));

        passed &= Case(ref cases, "minimum-width-one-pixel-rejected",
            !app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MinimumWindowWidthC179 - 1,
                ManagedTaskManagerC161.MinimumWindowHeightC179) &&
            app.WindowWidth == ManagedTaskManagerC161.MaximumWindowWidthC179 &&
            app.WindowHeight == ManagedTaskManagerC161.MaximumWindowHeightC179 &&
            LayoutIsValid(app.Controller));
        passed &= Case(ref cases, "minimum-height-one-pixel-rejected",
            !app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MinimumWindowWidthC179,
                ManagedTaskManagerC161.MinimumWindowHeightC179 - 1) &&
            app.WindowWidth == ManagedTaskManagerC161.MaximumWindowWidthC179 &&
            app.WindowHeight == ManagedTaskManagerC161.MaximumWindowHeightC179 &&
            LayoutIsValid(app.Controller));
        passed &= Case(ref cases, "maximum-width-one-pixel-rejected",
            !app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MaximumWindowWidthC179 + 1,
                ManagedTaskManagerC161.MaximumWindowHeightC179) &&
            app.WindowWidth == ManagedTaskManagerC161.MaximumWindowWidthC179 &&
            app.WindowHeight == ManagedTaskManagerC161.MaximumWindowHeightC179);
        passed &= Case(ref cases, "maximum-height-one-pixel-rejected",
            !app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MaximumWindowWidthC179,
                ManagedTaskManagerC161.MaximumWindowHeightC179 + 1) &&
            app.WindowWidth == ManagedTaskManagerC161.MaximumWindowWidthC179 &&
            app.WindowHeight == ManagedTaskManagerC161.MaximumWindowHeightC179);

        GuideXosTaskManagerControllerC161 rollbackController =
            ReadyController(CreateSnapshot(
                GuideXosTaskManagerControllerC161.ApplicationCapacity));
        rollbackController.List.SetFirstVisibleIndex(4);
        rollbackController.List.SelectIndex(13);
        rollbackController.SyncSelectionFromList();
        GuideXosApplicationInstanceId rollbackSelection =
            rollbackController.SelectedIdentity;
        int rollbackFocus = rollbackController.Controls.ActiveControlId;
        GuideXosManagedWindowResizeTransactionC174 rollback = new(
            in ManagedTaskManagerC161.ResizeBoundsC179,
            rollbackController.OuterWidth, rollbackController.OuterHeight,
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        GuideXosManagedWindowResizeCandidateC174 rollbackCandidate =
            rollback.Candidate;
        bool laidOut = rollbackController.TryRecomputeLayout(
            in rollbackCandidate);
        bool rejected = !rollback.Complete(layoutAccepted: laidOut,
            renderResult: GuideXosResult.Success, stateAccepted: false);
        bool rollbackRequired = rollback.RequiresRollback;
        bool restoredLayout = rollbackController.TryRecomputeLayout(
            rollback.PriorOuterWidth, rollback.PriorOuterHeight);
        bool restoredGeometry = rollbackController.OuterWidth ==
                ManagedTaskManagerC161.SurfaceWidth &&
            rollbackController.OuterHeight == ManagedTaskManagerC161.SurfaceHeight;
        bool restoredScroll = rollbackController.FirstVisibleIndex == 4;
        bool restoredSelection =
            rollbackController.SelectedIdentity == rollbackSelection;
        bool restoredFocus =
            rollbackController.Controls.ActiveControlId == rollbackFocus;
        bool restored = rollbackRequired && restoredLayout &&
            restoredGeometry && restoredScroll && restoredSelection && restoredFocus;
        passed &= Case(ref cases, "transaction-rejection-rolls-back-layout",
            laidOut && rejected && restored);

        bool actionEncoding = GuideXosManagedWindowGeometryC172.TryEncodeAction(
                ManagedTaskManagerC161.MinimumWindowWidthC179,
                ManagedTaskManagerC161.MinimumWindowHeightC179,
                out uint action) &&
            GuideXosManagedWindowGeometryC172.TryDecodeAction(action,
                out int decodedWidth, out int decodedHeight) &&
            decodedWidth == ManagedTaskManagerC161.MinimumWindowWidthC179 &&
            decodedHeight == ManagedTaskManagerC161.MinimumWindowHeightC179 &&
            !GuideXosManagedWindowGeometryC172.TryDecodeAction(
                ManagedTaskManagerC161.CloseActionId, out _, out _);
        passed &= Case(ref cases, "existing-action-channel-retained",
            actionEncoding);

        GuideXosTaskManagerControllerC161 empty = ReadyController(
            CreateSnapshot(0));
        uint emptyGeneration = empty.SnapshotGeneration;
        bool emptyResized = empty.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        passed &= Case(ref cases, "empty-snapshot-remains-empty",
            emptyResized && empty.RowCount == 0 && !empty.HasSelection &&
            empty.SnapshotGeneration == emptyGeneration &&
            empty.List.ItemCount == 0);

        GuideXosTaskManagerControllerC161 single = ReadyController(
            CreateSnapshot(1));
        bool singleResized = single.TryRecomputeLayout(
            ManagedTaskManagerC161.MinimumWindowWidthC179,
            ManagedTaskManagerC161.MinimumWindowHeightC179);
        passed &= Case(ref cases, "single-row-selection-remains-valid",
            singleResized && single.RowCount == 1 && single.SelectedIndex == 0 &&
            single.TryGetSelectedRecord(out _, out int singleIndex) &&
            singleIndex == 0 && single.FirstVisibleIndex == 0);

        GuideXosTaskManagerControllerC161 full = ReadyController(
            CreateSnapshot(GuideXosTaskManagerControllerC161.ApplicationCapacity));
        bool fullResized = full.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        passed &= Case(ref cases, "maximum-supported-snapshot-index-valid",
            fullResized && full.RowCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity &&
            full.List.ItemCount == full.RowCount &&
            full.List.MaximumItemCount == full.RowCount &&
            full.TryGetSelectedRecord(out _, out int fullIndex) &&
            fullIndex == full.SelectedIndex && full.SelectedIndex >= 0 &&
            full.SelectedIndex < full.RowCount);

        ManagedTaskManagerC161 refreshApp = new();
        bool refreshInitialized = refreshApp.InitializeForC179Tests();
        GuideXosApplicationSnapshot initialSnapshot = CreateSnapshot(
            GuideXosTaskManagerControllerC161.ApplicationCapacity);
        refreshApp.Controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in initialSnapshot);
        refreshApp.Controller.List.SetFirstVisibleIndex(4);
        refreshApp.Controller.List.SelectIndex(10);
        refreshApp.Controller.SyncSelectionFromList();
        ulong refreshWindow = refreshApp.WindowHandle;
        GuideXosApplicationInstanceId refreshSelection =
            refreshApp.Controller.SelectedIdentity;
        uint beforeResizeGeneration = refreshApp.Controller.SnapshotGeneration;
        int refreshFocus = refreshApp.Controller.Controls.ActiveControlId;
        bool refreshResize = refreshApp.TryApplyResizeForTests(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        bool preWindow = refreshWindow != 0u &&
            refreshApp.WindowHandle == refreshWindow;
        bool preGeneration = refreshApp.Controller.SnapshotGeneration ==
            beforeResizeGeneration;
        bool preSelection = refreshApp.Controller.SelectedIdentity == refreshSelection;
        bool preScroll = refreshApp.Controller.FirstVisibleIndex == 4;
        bool preFocus = refreshApp.Controller.Controls.ActiveControlId == refreshFocus;
        bool refreshInitialState = refreshInitialized && refreshResize &&
            preWindow && preGeneration && preSelection && preScroll && preFocus;
        GuideXosApplicationSnapshot changedSnapshot = CreateSnapshot(
            GuideXosTaskManagerControllerC161.ApplicationCapacity,
            firstIdentity: 1000, nameSuffix: " updated");
        uint beforeRefreshGeneration = refreshApp.Controller.SnapshotGeneration;
        bool refreshApplied = refreshApp.Controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in changedSnapshot);
        bool afterRefreshResize = refreshApp.TryApplyResizeForTests(
            ManagedTaskManagerC161.MinimumWindowWidthC179,
            ManagedTaskManagerC161.MinimumWindowHeightC179);
        bool refreshFinalState = refreshApplied &&
            refreshApp.Controller.SnapshotGeneration == beforeRefreshGeneration + 1u &&
            refreshApp.Controller.OuterWidth == ManagedTaskManagerC161.MinimumWindowWidthC179 &&
            refreshApp.Controller.OuterHeight == ManagedTaskManagerC161.MinimumWindowHeightC179 &&
            refreshApp.Controller.SelectedIdentity == refreshSelection &&
            afterRefreshResize && refreshApp.Controller.RowCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity &&
            refreshApp.Controller.SelectedIdentity == refreshSelection &&
            refreshApp.WindowHandle == refreshWindow;
        RefreshDetail = $"initial={refreshInitialState} final={refreshFinalState} init={refreshInitialized} resize={refreshResize} window={preWindow} generation={preGeneration} selection={preSelection} scroll={preScroll} focus={preFocus} applied={refreshApplied} genRefresh={refreshApp.Controller.SnapshotGeneration == beforeRefreshGeneration + 1u} geometry={refreshApp.Controller.OuterWidth}x{refreshApp.Controller.OuterHeight} selected={refreshApp.Controller.SelectedIdentity == refreshSelection} afterResize={afterRefreshResize} rows={refreshApp.Controller.RowCount}";
        passed &= Case(ref cases, "refresh-and-resize-retain-snapshot-selection-focus",
            refreshInitialState && refreshFinalState);

        GuideXosTaskManagerControllerC161 removed = ReadyController(
            CreateSnapshot(GuideXosTaskManagerControllerC161.ApplicationCapacity));
        removed.List.SelectIndex(15);
        removed.SyncSelectionFromList();
        GuideXosApplicationInstanceId removedIdentity = removed.SelectedIdentity;
        GuideXosApplicationSnapshot shorter = CreateSnapshot(12);
        bool removedRefreshed = removed.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in shorter);
        bool removedResize = removed.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        passed &= Case(ref cases, "removed-selection-stays-cleared",
            removedRefreshed && removedResize && removedIdentity.Value != 0u &&
            removed.RowCount == 12 && !removed.HasSelection &&
            removed.SelectedIndex == -1 && !removed.TryGetSelectedRecord(out _, out _));

        GuideXosTaskManagerControllerC161 scroll = ReadyController(
            CreateSnapshot(GuideXosTaskManagerControllerC161.ApplicationCapacity));
        scroll.List.SetFirstVisibleIndex(0);
        scroll.List.SelectIndex(
            GuideXosTaskManagerControllerC161.ApplicationCapacity - 2);
        scroll.SyncSelectionFromList();
        GuideXosApplicationInstanceId scrollSelection = scroll.SelectedIdentity;
        bool scrollMax = scroll.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        int expandedOffset = scroll.FirstVisibleIndex;
        int expandedVisibleRows = scroll.List.VisibleRowCount;
        bool scrollMin = scroll.TryRecomputeLayout(
            ManagedTaskManagerC161.MinimumWindowWidthC179,
            ManagedTaskManagerC161.MinimumWindowHeightC179);
        int expectedExpandedOffset = Math.Max(0,
            GuideXosTaskManagerControllerC161.ApplicationCapacity -
                expandedVisibleRows);
        int expectedMinimumOffset = Math.Max(0,
            scroll.SelectedIndex - scroll.List.VisibleRowCount + 1);
        passed &= Case(ref cases, "scroll-and-selected-row-clamp-across-resize",
            scrollMax && expandedOffset == expectedExpandedOffset && scrollMin &&
            scroll.FirstVisibleIndex == expectedMinimumOffset && scroll.HasSelection &&
            scroll.SelectedIdentity == scrollSelection &&
            scroll.SelectedIndex ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity - 2 &&
            scroll.FirstVisibleIndex <= scroll.List.MaximumFirstVisibleIndex);

        GuideXosTaskManagerControllerC161 pointer = ReadyController(
            CreateSnapshot(GuideXosTaskManagerControllerC161.ApplicationCapacity));
        bool pointerExpanded = pointer.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        GuideXosTaskManagerCommandC161 pointerCommand = pointer.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                GuideXosTaskManagerControllerC161.ListX +
                    pointer.List.Width - 1,
                GuideXosTaskManagerControllerC161.ListY + 3 *
                    pointer.List.LineHeight + 3));
        passed &= Case(ref cases, "expanded-list-pointer-hit-test-matches-bounds",
            pointerExpanded && pointerCommand == GuideXosTaskManagerCommandC161.None &&
            pointer.SelectedIndex == 3 && pointer.HasSelection &&
            pointer.List.Width == pointer.List.RenderWidth *
                GuideXosLabel.CharacterWidth);

        GuideXosTaskManagerControllerC161 keyboard = ReadyController(
            CreateSnapshot(GuideXosTaskManagerControllerC161.ApplicationCapacity));
        bool keyboardResized = keyboard.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        int keyboardFocus = keyboard.Controls.ActiveControlId;
        keyboard.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down));
        passed &= Case(ref cases, "keyboard-navigation-after-resize",
            keyboardResized && keyboard.SelectedIndex == 1 &&
            keyboard.SelectedIdentity.Value == 1001UL &&
            keyboard.Controls.ActiveControlId == keyboardFocus &&
            keyboardFocus == GuideXosTaskManagerControllerC161.ListControlId);

        GuideXosTaskManagerControllerC161 refreshButton = ReadyController(
            CreateSnapshot(1));
        bool refreshButtonResized = refreshButton.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        GuideXosTaskManagerCommandC161 refreshCommand = refreshButton.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                refreshButton.RefreshButton.X + 8,
                refreshButton.RefreshButton.Y + 8));
        passed &= Case(ref cases, "moved-refresh-button-remains-activatable",
            refreshButtonResized &&
            refreshButton.RefreshButton.Y == refreshButton.ButtonRowY &&
            refreshCommand == GuideXosTaskManagerCommandC161.Refresh &&
            refreshButton.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.RefreshControlId);

        GuideXosTaskManagerControllerC161 closeButton = ReadyController(
            CreateSnapshot(1));
        bool closeButtonResized = closeButton.TryRecomputeLayout(
            ManagedTaskManagerC161.MaximumWindowWidthC179,
            ManagedTaskManagerC161.MaximumWindowHeightC179);
        GuideXosTaskManagerCommandC161 closeCommand = closeButton.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                closeButton.CloseButton.X + 8,
                closeButton.CloseButton.Y + 8));
        passed &= Case(ref cases, "moved-close-button-keeps-existing-command",
            closeButtonResized && closeCommand ==
                GuideXosTaskManagerCommandC161.Close && closeButton.HasSnapshot);

        bool currentViewAndDataRetained =
            refreshApp.Controller.SnapshotGeneration == beforeRefreshGeneration + 1u &&
            refreshApp.Controller.RowCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity &&
            refreshApp.Controller.LastResult ==
                GuideXosApplicationSnapshotResult.Success;
        passed &= Case(ref cases, "resize-does-not-refresh-or-change-view",
            currentViewAndDataRetained && refreshApp.WindowHandle == refreshWindow &&
            refreshApp.Controller.Controls.ActiveControlId == refreshFocus);

        if (cases != CaseCount)
        {
            FirstFailedCase = string.IsNullOrEmpty(FirstFailedCase)
                ? $"case-count-{cases}" : FirstFailedCase;
            passed = false;
        }
        host?.TryLog(passed
            ? "C179-TM-RESIZE focused=20 snapshot=stable selection=stable scroll=PASS result=PASS"u8
            : "C179-TM-RESIZE result=FAIL"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        LayoutUpdates = AlternatingResizes = RefreshResizeCycles =
            RejectedGeometryChecks = LifecycleCycles = GeometryDriftCount = 0;
        ResizeAllocationBytes = RefreshAllocationBytes = 0;
        FirstFailedCase = string.Empty;
        RefreshDetail = string.Empty;

        ManagedTaskManagerC161 app = new();
        if (!app.InitializeForC179Tests())
        {
            FirstFailedCase = "stress-initialize";
            return false;
        }
        GuideXosApplicationSnapshot maximumSnapshot = CreateSnapshot(
            GuideXosTaskManagerControllerC161.ApplicationCapacity);
        GuideXosApplicationSnapshotResult fullResult =
            GuideXosApplicationSnapshotResult.Success;
        if (!app.Controller.ApplySnapshot(fullResult, in maximumSnapshot))
        {
            FirstFailedCase = "stress-snapshot";
            return false;
        }
        app.Controller.List.SelectIndex(
            GuideXosTaskManagerControllerC161.ApplicationCapacity - 2);
        app.Controller.SyncSelectionFromList();
        GuideXosApplicationInstanceId retainedIdentity =
            app.Controller.SelectedIdentity;
        int initialFocus = app.Controller.Controls.ActiveControlId;

        long resizeBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < RequiredLayoutUpdates; ++index)
        {
            int width = (index % 3) switch
            {
                0 => ManagedTaskManagerC161.MinimumWindowWidthC179,
                1 => ManagedTaskManagerC161.SurfaceWidth,
                _ => ManagedTaskManagerC161.MaximumWindowWidthC179,
            };
            int height = (index % 3) switch
            {
                0 => ManagedTaskManagerC161.MinimumWindowHeightC179,
                1 => ManagedTaskManagerC161.SurfaceHeight,
                _ => ManagedTaskManagerC161.MaximumWindowHeightC179,
            };
            if (!app.TryApplyResizeForTests(width, height) ||
                !LayoutIsValid(app.Controller) ||
                app.Controller.SelectedIdentity != retainedIdentity ||
                app.Controller.Controls.ActiveControlId != initialFocus)
            {
                FirstFailedCase = $"stress-layout-{index}";
                return false;
            }
            ++LayoutUpdates;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            resizeBefore;

        if (!app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MinimumWindowWidthC179,
                ManagedTaskManagerC161.MinimumWindowHeightC179))
        {
            FirstFailedCase = "stress-alternating-start";
            return false;
        }
        for (int index = 0; index < RequiredAlternatingResizes; ++index)
        {
            bool maximum = (index & 1) == 0;
            int width = maximum
                ? ManagedTaskManagerC161.MaximumWindowWidthC179
                : ManagedTaskManagerC161.MinimumWindowWidthC179;
            int height = maximum
                ? ManagedTaskManagerC161.MaximumWindowHeightC179
                : ManagedTaskManagerC161.MinimumWindowHeightC179;
            if (!app.TryApplyResizeForTests(width, height) ||
                !LayoutIsValid(app.Controller) ||
                app.Controller.SelectedIdentity != retainedIdentity)
            {
                FirstFailedCase = $"stress-alternating-{index}";
                return false;
            }
            ++AlternatingResizes;
        }
        if (app.WindowWidth != ManagedTaskManagerC161.MinimumWindowWidthC179 ||
            app.WindowHeight != ManagedTaskManagerC161.MinimumWindowHeightC179)
            ++GeometryDriftCount;
        if (!app.TryApplyResizeForTests(ManagedTaskManagerC161.SurfaceWidth,
                ManagedTaskManagerC161.SurfaceHeight) ||
            app.Controller.List.RenderWidth != 45 ||
            app.Controller.List.VisibleRowCount != 10 ||
            app.Controller.DetailsX != 400 || app.Controller.StatusY != 248 ||
            app.Controller.ButtonRowY != 276)
            ++GeometryDriftCount;

        GuideXosApplicationSnapshot changedSnapshot = CreateSnapshot(
            GuideXosTaskManagerControllerC161.ApplicationCapacity,
            firstIdentity: 1000, nameSuffix: " refreshed");
        for (int index = 0; index < RequiredRefreshResizeCycles; ++index)
        {
            uint generation = app.Controller.SnapshotGeneration;
            GuideXosApplicationSnapshot snapshot =
                (index & 1) == 0 ? maximumSnapshot : changedSnapshot;
            long refreshBefore = GC.GetAllocatedBytesForCurrentThread();
            if (!app.Controller.ApplySnapshot(fullResult, in snapshot) ||
                app.Controller.SnapshotGeneration != generation + 1u ||
                app.Controller.SelectedIdentity != retainedIdentity)
            {
                FirstFailedCase = $"stress-refresh-{index}";
                return false;
            }
            RefreshAllocationBytes += GC.GetAllocatedBytesForCurrentThread() -
                refreshBefore;
            bool max = (index & 1) == 0;
            if (!app.TryApplyResizeForTests(
                    max ? ManagedTaskManagerC161.MaximumWindowWidthC179 :
                        ManagedTaskManagerC161.MinimumWindowWidthC179,
                    max ? ManagedTaskManagerC161.MaximumWindowHeightC179 :
                        ManagedTaskManagerC161.MinimumWindowHeightC179) ||
                app.Controller.SelectedIdentity != retainedIdentity ||
                !LayoutIsValid(app.Controller))
            {
                FirstFailedCase = $"stress-refresh-resize-{index}";
                return false;
            }
            ++RefreshResizeCycles;
        }
        if (!app.TryApplyResizeForTests(
                ManagedTaskManagerC161.MinimumWindowWidthC179,
                ManagedTaskManagerC161.MinimumWindowHeightC179))
        {
            FirstFailedCase = "stress-reject-start";
            return false;
        }
        int priorOffset = app.Controller.FirstVisibleIndex;
        int priorSelected = app.Controller.SelectedIndex;
        int priorFocus = app.Controller.Controls.ActiveControlId;
        uint priorGeneration = app.Controller.SnapshotGeneration;
        for (int index = 0; index < RequiredRejectedGeometryChecks; ++index)
        {
            if (app.TryApplyResizeForTests(
                    ManagedTaskManagerC161.MinimumWindowWidthC179 - 1,
                    ManagedTaskManagerC161.MinimumWindowHeightC179) ||
                app.WindowWidth != ManagedTaskManagerC161.MinimumWindowWidthC179 ||
                app.WindowHeight != ManagedTaskManagerC161.MinimumWindowHeightC179 ||
                app.Controller.FirstVisibleIndex != priorOffset ||
                app.Controller.SelectedIndex != priorSelected ||
                app.Controller.Controls.ActiveControlId != priorFocus ||
                app.Controller.SnapshotGeneration != priorGeneration ||
                app.Controller.SelectedIdentity != retainedIdentity)
            {
                FirstFailedCase = $"stress-rejected-{index}";
                return false;
            }
            ++RejectedGeometryChecks;
        }

        for (int index = 0; index < RequiredLifecycleCycles; ++index)
        {
            ManagedTaskManagerC161 instance = new();
            if (!instance.InitializeForC179Tests() ||
                !instance.Controller.ApplySnapshot(fullResult,
                    in maximumSnapshot) ||
                !instance.TryApplyResizeForTests(
                    ManagedTaskManagerC161.MaximumWindowWidthC179,
                    ManagedTaskManagerC161.MaximumWindowHeightC179))
            {
                FirstFailedCase = $"stress-lifecycle-start-{index}";
                return false;
            }
            instance.OnTearingDown();
            if (instance.WindowHandle != 0u || instance.ControlCount != 0 ||
                instance.Controller.HasSnapshot ||
                instance.Controller.HasSelection ||
                instance.WindowWidth != ManagedTaskManagerC161.SurfaceWidth ||
                instance.WindowHeight != ManagedTaskManagerC161.SurfaceHeight ||
                instance.Controller.Controls.ActiveControlId != 0)
            {
                FirstFailedCase = $"stress-lifecycle-teardown-{index}";
                return false;
            }
            ++LifecycleCycles;
        }

        bool passed = LayoutUpdates == RequiredLayoutUpdates &&
            AlternatingResizes == RequiredAlternatingResizes &&
            RefreshResizeCycles == RequiredRefreshResizeCycles &&
            RejectedGeometryChecks == RequiredRejectedGeometryChecks &&
            LifecycleCycles == RequiredLifecycleCycles &&
            ResizeAllocationBytes == 0 && GeometryDriftCount == 0;
        FirstFailedCase = passed ? string.Empty : "stress-final-invariant";
        host?.TryLog(passed
            ? "C179-TM-RESIZE-STRESS layout=1000 alternating=500 refreshResize=250 rejected=250 lifecycles=100 resizeAllocationBytes=0 refreshAllocationBytes=0 drift=0 result=PASS"u8
            : "C179-TM-RESIZE-STRESS result=FAIL"u8);
        return passed;
    }

    private static GuideXosTaskManagerControllerC161 ReadyController(
        GuideXosApplicationSnapshot snapshot)
    {
        GuideXosTaskManagerControllerC161 controller = new();
        controller.InitializeControls();
        controller.ApplySnapshot(GuideXosApplicationSnapshotResult.Success,
            in snapshot);
        return controller;
    }

    private static GuideXosApplicationSnapshot CreateSnapshot(int count,
        ulong firstIdentity = 1000, string nameSuffix = "")
    {
        GuideXosApplicationSnapshot snapshot = default;
        snapshot.totalCount = (uint)count;
        snapshot.copiedCount = (uint)count;
        snapshot.nativeResult = 0;
        for (int index = 0; index < count; ++index)
        {
            string name = "Application " + index.ToString() + nameSuffix;
            const string appId = "com.guidexos.c179.test";
            GuideXosApplicationSnapshotRecord record = default;
            record.recordVersion = GxAbi.ApplicationSnapshotRecordVersion;
            record.source = GuideXosApplicationSnapshotSource.AppManagerInstance;
            record.instanceId = firstIdentity + (uint)index;
            record.state = GuideXosApplicationSnapshotState.Running;
            record.flags = index == 0 ? 1u : 0u;
            record.displayNameLength = (uint)name.Length;
            record.applicationIdLength = (uint)appId.Length;
            byte* nameTarget = record.displayName;
            for (int character = 0; character < name.Length; ++character)
                nameTarget[character] = (byte)name[character];
            nameTarget[name.Length] = 0;
            byte* appIdTarget = record.applicationId;
            for (int character = 0; character < appId.Length; ++character)
                appIdTarget[character] = (byte)appId[character];
            appIdTarget[appId.Length] = 0;
            ulong* storage = snapshot.recordStorage;
            *(GuideXosApplicationSnapshotRecord*)((byte*)storage +
                (int)(index * GxAbi.ApplicationSnapshotRecordSize)) = record;
        }
        return snapshot;
    }

    private static bool DetailsFit(
        GuideXosTaskManagerControllerC161 controller) =>
        controller.DetailsX >= 0 &&
        controller.DetailsX + 400 <= controller.ClientWidth &&
        controller.List.X + controller.List.Width + 20 == controller.DetailsX;

    private static bool LayoutIsValid(
        GuideXosTaskManagerControllerC161 controller) =>
        controller.ClientWidth >= 800 && controller.ClientHeight >= 320 &&
        controller.List.RenderWidth is >= 45 and <= 48 &&
        controller.List.VisibleRowCount is >= 10 and <= 16 &&
        controller.List.X == GuideXosTaskManagerControllerC161.ListX &&
        controller.List.Y == GuideXosTaskManagerControllerC161.ListY &&
        controller.List.X + controller.List.Width <= controller.ClientWidth &&
        controller.List.Y + controller.List.Height <= controller.StatusY - 10 &&
        controller.StatusY + 18 + 10 == controller.ButtonRowY &&
        controller.ButtonRowY + controller.RefreshButton.Height <=
            controller.ClientHeight - 16 &&
        controller.CloseButton.X + controller.CloseButton.Width <=
            controller.ClientWidth && DetailsFit(controller) &&
        controller.SelectedIndex < controller.RowCount &&
        (controller.SelectedIndex < 0 ||
         controller.SelectedIndex >= controller.FirstVisibleIndex &&
         controller.SelectedIndex < controller.FirstVisibleIndex +
            controller.List.VisibleRowCount);

    private static bool Case(ref int cases, string name, bool passed)
    {
        ++cases;
        if (!passed && string.IsNullOrEmpty(FirstFailedCase))
            FirstFailedCase = name;
        return passed;
    }
}
#endif
