#if HOSTLOGPROOF_C173_MANAGED_FILE_EXPLORER_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedWindowResizeC173Tests
{
    public const int CaseCount = 47;
    public const int StressCaseCount = 10;
    public const int LayoutUpdateCount = 1000;
    public const int AlternatingResizeCount = 500;
    public const int SelectionTransitionCount = 500;
    public const int ScrollOperationCount = 250;
    public const int KeyboardNavigationCount = 250;
    public const int NavigationResizeCount = 100;
    public const int LifecycleCount = 100;

    public static int CasesExecuted { get; private set; }
    public static int FirstFailedCase { get; private set; }
    public static int StressLayoutUpdates { get; private set; }
    public static int StressAlternatingResizes { get; private set; }
    public static int StressSelectionTransitions { get; private set; }
    public static int StressScrollOperations { get; private set; }
    public static int StressKeyboardNavigations { get; private set; }
    public static int StressNavigationResizeOperations { get; private set; }
    public static int StressLifecycleCycles { get; private set; }
    public static long StressLayoutAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }
    public static string StressFailureDetail { get; private set; } = "not-run";

    private sealed class FakeSource : GuideXosFileExplorerSourceC163
    {
        public GuideXosDirectoryEntry[] RootEntries = CreateRootEntries();
        public GuideXosDirectoryEntry[] DocsEntries =
        {
            new("child-00.txt", GuideXosEntryType.Regular, 10u),
            new("child-01.txt", GuideXosEntryType.Regular, 20u),
        };
        public int LoadCalls;
        public int StatCalls;

        public override GuideXosFileResult Load(string path,
            out GuideXosDirectoryEntry[] entries, out bool hasMore)
        {
            ++LoadCalls;
            hasMore = false;
            if (path == GuideXosDirectoryBrowserC163.InitialPath)
            {
                entries = RootEntries;
                return GuideXosFileResult.Success;
            }
            if (path == GuideXosDirectoryBrowserC163.InitialPath + "/Docs")
            {
                entries = DocsEntries;
                return GuideXosFileResult.Success;
            }
            if (path == GuideXosDirectoryBrowserC163.InitialPath + "/Empty")
            {
                entries = Array.Empty<GuideXosDirectoryEntry>();
                return GuideXosFileResult.Success;
            }
            entries = Array.Empty<GuideXosDirectoryEntry>();
            return GuideXosFileResult.NotFound;
        }

        public override GuideXosFileResult Stat(string path,
            out GuideXosFileInfo info)
        {
            ++StatCalls;
            info = null;
            if (path.EndsWith("/Docs", StringComparison.Ordinal) ||
                path.EndsWith("/Empty", StringComparison.Ordinal))
            {
                info = new GuideXosFileInfo(GuideXosEntryType.Directory, 0u);
                return GuideXosFileResult.Success;
            }
            if (path.EndsWith(".txt", StringComparison.Ordinal))
            {
                info = new GuideXosFileInfo(GuideXosEntryType.Regular, 10u);
                return GuideXosFileResult.Success;
            }
            return GuideXosFileResult.NotFound;
        }

        private static GuideXosDirectoryEntry[] CreateRootEntries()
        {
            GuideXosDirectoryEntry[] entries =
                new GuideXosDirectoryEntry[GuideXosDirectoryBrowserC163.EntryCapacity];
            entries[0] = new GuideXosDirectoryEntry(
                "Docs", GuideXosEntryType.Directory, 0u);
            entries[1] = new GuideXosDirectoryEntry(
                "Empty", GuideXosEntryType.Directory, 0u);
            for (int index = 0; index < entries.Length - 2; index++)
            {
                entries[index + 2] = new GuideXosDirectoryEntry(
                    "item-" + index.ToString("D2") + ".txt",
                    GuideXosEntryType.Regular, (ulong)index);
            }
            return entries;
        }
    }

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        FirstFailedCase = 0;
        FakeSource source = new();
        GuideXosFileExplorerControllerC163 controller = NewController(source);
        GuideXosFileExplorerControllerC163 sameController = controller;
        GuideXosDirectoryBrowserC163 browser = controller.Browser;
        GuideXosListBox list = controller.List;

        passed &= Case(ref cases, controller.WindowWidth == 800 &&
            controller.WindowHeight == 390 && controller.ListVisibleRowCount == 12 &&
            controller.ListRenderWidth == 59 && controller.ScrollBarVisible);
        passed &= Case(ref cases, controller.RowCount == 64 &&
            browser.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath);
        passed &= Case(ref cases, LayoutWithinClient(controller));
        int fileIndex = FindIndex(browser, "item-40.txt");
        passed &= Case(ref cases, fileIndex >= 0 && browser.Select(fileIndex) &&
            list.SelectIndex(fileIndex) != GuideXosListBoxResult.Rejected);
        passed &= Case(ref cases, browser.SelectedEntry?.Name == "item-40.txt" &&
            browser.SelectedEntry.Type == GuideXosEntryType.Regular);

        int loadsBeforeResize = source.LoadCalls;
        int statsBeforeResize = source.StatCalls;
        int priorFocus = controller.Controls.ActiveControlId;
        passed &= Case(ref cases, controller.TryRecomputeLayout(960, 526));
        passed &= Case(ref cases, controller.WindowWidth == 960 &&
            controller.WindowHeight == 526 && controller.ListVisibleRowCount == 16 &&
            controller.ListRenderWidth == 79 && controller.ScrollBarVisible &&
            controller.ScrollBar.Maximum > 0);
        passed &= Case(ref cases, LayoutWithinClient(controller));
        passed &= Case(ref cases, browser.CurrentPath ==
            GuideXosDirectoryBrowserC163.InitialPath &&
            browser.SelectedEntry?.Name == "item-40.txt");
        passed &= Case(ref cases, controller.RowCount == 64 &&
            controller.Controls.RegistrationCount == 6 &&
            controller.Controls.ActiveControlId == priorFocus);
        passed &= Case(ref cases, source.LoadCalls == loadsBeforeResize &&
            source.StatCalls == statsBeforeResize);
        passed &= Case(ref cases, ReferenceEquals(controller, sameController) &&
            ReferenceEquals(controller.Browser, browser) &&
            ReferenceEquals(controller.List, list));

        int stableWidth = controller.ListWidth;
        int stableHeight = controller.ListHeight;
        int stableX = controller.DetailX;
        int stableY = controller.StatusY;
        int stableOffset = controller.FirstVisibleIndex;
        for (int index = 0; index < 64; index++)
            passed &= controller.TryRecomputeLayout(960, 526);
        passed &= Case(ref cases, controller.ListWidth == stableWidth &&
            controller.ListHeight == stableHeight && controller.DetailX == stableX &&
            controller.StatusY == stableY &&
            controller.FirstVisibleIndex == stableOffset);

        passed &= Case(ref cases, controller.TryRecomputeLayout(600, 360));
        passed &= Case(ref cases, controller.WindowWidth == 600 &&
            controller.WindowHeight == 360 && controller.ListVisibleRowCount == 10 &&
            controller.ListRenderWidth == 34 && controller.ScrollBarVisible);
        passed &= Case(ref cases, LayoutWithinClient(controller));
        passed &= Case(ref cases, controller.ListXPosition + controller.ListWidth <=
            controller.DetailX && controller.DetailX + 256 <= 600);
        passed &= Case(ref cases, browser.SelectedEntry?.Name == "item-40.txt" &&
            browser.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath);
        passed &= Case(ref cases, controller.FirstVisibleIndex >= 0 &&
            controller.FirstVisibleIndex <= list.MaximumFirstVisibleIndex);

        int priorWidth = controller.WindowWidth;
        int priorHeight = controller.WindowHeight;
        int priorRows = list.VisibleRowCount;
        int priorRenderWidth = list.RenderWidth;
        int priorOffset = list.FirstVisibleIndex;
        passed &= Case(ref cases, !controller.TryRecomputeLayout(599, 390));
        passed &= Case(ref cases, !controller.TryRecomputeLayout(600, 359));
        passed &= Case(ref cases, !controller.TryRecomputeLayout(961, 500));
        passed &= Case(ref cases, !controller.TryRecomputeLayout(800, 527));
        passed &= Case(ref cases, controller.WindowWidth == priorWidth &&
            controller.WindowHeight == priorHeight &&
            list.VisibleRowCount == priorRows && list.RenderWidth == priorRenderWidth &&
            list.FirstVisibleIndex == priorOffset);

        int selectedIndex = FindIndex(browser, "item-40.txt");
        passed &= Case(ref cases, selectedIndex > 0 &&
            list.SetFirstVisibleIndex(selectedIndex - 5) &&
            list.SelectIndex(selectedIndex) != GuideXosListBoxResult.Rejected &&
            browser.Select(selectedIndex));
        int nonzeroOffset = list.FirstVisibleIndex;
        passed &= Case(ref cases, nonzeroOffset > 0 &&
            controller.TryRecomputeLayout(960, 526) &&
            list.FirstVisibleIndex <= list.MaximumFirstVisibleIndex &&
            selectedIndex >= list.FirstVisibleIndex &&
            selectedIndex < list.FirstVisibleIndex + list.VisibleRowCount);
        passed &= Case(ref cases, controller.TryRecomputeLayout(600, 360) &&
            list.FirstVisibleIndex <= list.MaximumFirstVisibleIndex &&
            selectedIndex >= list.FirstVisibleIndex &&
            selectedIndex < list.FirstVisibleIndex + list.VisibleRowCount);

        int scrollOffsetBeforeDrag = list.FirstVisibleIndex;
        int scrollThumbY = controller.ScrollBar.ThumbTop +
            Math.Max(0, controller.ScrollBar.ThumbHeight / 2);
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            controller.ScrollBar.X + 8, scrollThumbY));
        bool scrollbarDragStarted = controller.Controls.HasPointerDragCapture &&
            controller.ScrollBar.IsDragging;
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerMove, GuideXosPointerButton.Primary,
            controller.ScrollBar.X + 8,
            controller.ScrollBar.Y + controller.ScrollBar.Height - 13));
        bool scrollbarMoved = list.FirstVisibleIndex != scrollOffsetBeforeDrag;
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerUp, GuideXosPointerButton.Primary,
            controller.ScrollBar.X + 8,
            controller.ScrollBar.Y + controller.ScrollBar.Height - 13));
        passed &= Case(ref cases, scrollbarDragStarted && scrollbarMoved &&
            list.FirstVisibleIndex <= list.MaximumFirstVisibleIndex);
        passed &= Case(ref cases, !controller.Controls.HasPointerDragCapture &&
            !controller.ScrollBar.IsDragging &&
            controller.ScrollBar.Value == list.FirstVisibleIndex);

        int row = browser.SelectedIndex - list.FirstVisibleIndex;
        int pointerX = list.X + 8;
        int pointerY = list.Y + row * list.LineHeight + 8;
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            pointerX, pointerY));
        passed &= Case(ref cases, browser.SelectedIndex == selectedIndex &&
            browser.SelectedEntry?.Name == "item-40.txt");
        int priorSelection = browser.SelectedIndex;
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            400, list.Y + 8));
        passed &= Case(ref cases, browser.SelectedIndex == priorSelection);
        controller.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            list.X + 8, list.Y + list.Height + 4));
        passed &= Case(ref cases, browser.SelectedIndex == priorSelection);

        controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down));
        passed &= Case(ref cases, browser.SelectedIndex == priorSelection + 1 &&
            list.SelectedIndex == browser.SelectedIndex);
        int focusBeforeTab = controller.Controls.ActiveControlId;
        GuideXosControlHostResult tab = controller.Controls.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab));
        passed &= Case(ref cases, tab == GuideXosControlHostResult.Traversed &&
            controller.Controls.ActiveControlId != focusBeforeTab);
        GuideXosControlHostResult shiftTab = controller.Controls.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, shift: true));
        passed &= Case(ref cases, shiftTab == GuideXosControlHostResult.Traversed &&
            controller.Controls.ActiveControlId == focusBeforeTab);
        passed &= Case(ref cases, !controller.Controls.IsModalActive &&
            !controller.Controls.HasTransientInputCapture &&
            !controller.Controls.HasPointerDragCapture);

        int oldLoads = source.LoadCalls;
        int oldStats = source.StatCalls;
        controller.RouteInput(GuideXosInputEvent.ForWheel(
            list.X + 8, list.Y + 8, -1));
        passed &= Case(ref cases, source.LoadCalls == oldLoads &&
            source.StatCalls == oldStats && browser.CurrentPath ==
                GuideXosDirectoryBrowserC163.InitialPath);
        passed &= Case(ref cases, list.FirstVisibleIndex >= 0 &&
            list.FirstVisibleIndex <= list.MaximumFirstVisibleIndex &&
            controller.ScrollBar.Value == list.FirstVisibleIndex);

        FakeSource longSource = new();
        string longName = new('L', 127);
        longSource.RootEntries = new[]
        {
            new GuideXosDirectoryEntry(longName, GuideXosEntryType.Regular, 0u),
        };
        GuideXosFileExplorerControllerC163 longController = NewController(longSource);
        passed &= Case(ref cases, longController.Browser.EntryCount == 1 &&
            longController.Browser.EntryAt(0).Name == longName &&
            longController.List.GetItemLabel(0).Length ==
                GuideXosListBox.MaximumSupportedLabelLength);
        passed &= Case(ref cases, longController.TryRecomputeLayout(600, 360) &&
            longController.Browser.EntryAt(0).Name == longName &&
            longController.List.RenderWidth == 34);
        passed &= Case(ref cases, longController.PathWidth >= 560 &&
            longController.ListWidth <= longController.DetailX -
                longController.ListXPosition);

        FakeSource navigationSource = new();
        GuideXosFileExplorerControllerC163 navigation = NewController(navigationSource);
        passed &= Case(ref cases, navigation.Browser.Select(0) &&
            navigation.OpenSelected() == GuideXosFileResult.Success &&
            navigation.Browser.CurrentPath ==
                GuideXosDirectoryBrowserC163.InitialPath + "/Docs");
        int childLoads = navigationSource.LoadCalls;
        string childPath = navigation.Browser.CurrentPath;
        int childCount = navigation.Browser.EntryCount;
        passed &= Case(ref cases, navigation.TryRecomputeLayout(960, 526) &&
            navigation.Browser.CurrentPath == childPath &&
            navigation.Browser.EntryCount == childCount &&
            navigationSource.LoadCalls == childLoads &&
            !navigation.ScrollBarVisible);
        passed &= Case(ref cases, navigation.Browser.Select(1) &&
            navigation.Browser.SelectedEntry?.Name == "child-01.txt");

        GuideXosFileExplorerControllerC163 empty = NewController(new FakeSource
        {
            RootEntries = Array.Empty<GuideXosDirectoryEntry>(),
        });
        passed &= Case(ref cases, empty.Browser.EntryCount == 0 &&
            !empty.Browser.HasSelection && empty.TryRecomputeLayout(600, 360) &&
            empty.TryRecomputeLayout(960, 526) && empty.List.ItemCount == 0 &&
            !empty.ScrollBarVisible);

        passed &= Case(ref cases, controller.Controls.RegistrationCount == 6 &&
            controller.RowCount == 64 &&
            !controller.Controls.HasTransientInputCapture &&
            !controller.Controls.HasPointerDragCapture);
        passed &= Case(ref cases, controller.WindowWidth == 600 &&
            controller.WindowHeight == 360 && LayoutWithinClient(controller));

        CasesExecuted = cases;
        bool result = passed && cases == CaseCount;
        host?.TryLog(result
            ? "C173-WINDOW-RESIZE cases=48 path=C172-managed-action result=PASS"u8
            : "C173-WINDOW-RESIZE result=FAIL"u8);
        return result;
    }

    public static bool RunStress(GuideXosHost host)
    {
        FakeSource source = new();
        GuideXosFileExplorerControllerC163 controller = NewController(source);
        bool ready = controller.RowCount == 64 && controller.TryRecomputeLayout(
            Applications.ManagedFileExplorerC163.SurfaceWidth,
            Applications.ManagedFileExplorerC163.SurfaceHeight);
        int baselineWidth = controller.WindowWidth;
        int baselineHeight = controller.WindowHeight;
        int baselineControlCount = controller.Controls.RegistrationCount;
        int baselineLoads = source.LoadCalls;
        int baselineStats = source.StatCalls;

        _ = GC.GetAllocatedBytesForCurrentThread();
        long allocationBefore = GC.GetAllocatedBytesForCurrentThread();
        bool layouts = ready;
        for (int index = 0; index < LayoutUpdateCount; index++)
        {
            bool large = (index & 1) == 0;
            layouts &= controller.TryRecomputeLayout(
                large ? 960 : 600, large ? 526 : 360);
        }
        StressLayoutAllocationBytes =
            GC.GetAllocatedBytesForCurrentThread() - allocationBefore;
        StressLayoutUpdates = LayoutUpdateCount;
        StressAlternatingResizes = AlternatingResizeCount;

        int fileIndex = FindIndex(controller.Browser, "item-40.txt");
        bool selections = fileIndex >= 0;
        for (int index = 0; index < SelectionTransitionCount; index++)
        {
            int target = index % controller.Browser.EntryCount;
            selections &= controller.Browser.Select(target) &&
                controller.List.SelectIndex(target) != GuideXosListBoxResult.Rejected;
        }
        StressSelectionTransitions = SelectionTransitionCount;

        bool scrolling = controller.TryRecomputeLayout(600, 360) &&
            controller.List.MaximumFirstVisibleIndex > 0;
        for (int index = 0; index < ScrollOperationCount; index++)
        {
            int offset = (index * 7) %
                (controller.List.MaximumFirstVisibleIndex + 1);
            scrolling &= controller.List.SetFirstVisibleIndex(offset) &&
                controller.List.FirstVisibleIndex <=
                    controller.List.MaximumFirstVisibleIndex;
        }
        StressScrollOperations = ScrollOperationCount;

        controller.Controls.TryFocus(
            GuideXosFileExplorerControllerC163.ListControlId);
        bool keyboard = true;
        for (int index = 0; index < KeyboardNavigationCount; index++)
        {
            controller.RouteInput(GuideXosInputEvent.ForKeyDown(
                (index & 1) == 0 ? GuideXosTextInputKey.Down :
                    GuideXosTextInputKey.Up));
            keyboard &= controller.Browser.SelectedIndex ==
                controller.List.SelectedIndex;
        }
        StressKeyboardNavigations = KeyboardNavigationCount;

        bool navigation = true;
        for (int index = 0; index < NavigationResizeCount; index++)
        {
            navigation &= controller.Browser.Select(0) &&
                controller.OpenSelected() == GuideXosFileResult.Success &&
                controller.TryRecomputeLayout(
                    (index & 1) == 0 ? 960 : 600,
                    (index & 1) == 0 ? 526 : 360) &&
                controller.NavigateUp() == GuideXosFileResult.Success &&
                controller.TryRecomputeLayout(
                    (index & 1) == 0 ? 600 : 960,
                    (index & 1) == 0 ? 360 : 526) &&
                controller.Browser.CurrentPath ==
                    GuideXosDirectoryBrowserC163.InitialPath;
        }
        StressNavigationResizeOperations = NavigationResizeCount;

        bool lifecycle = true;
        for (int index = 0; index < LifecycleCount; index++)
        {
            GuideXosFileExplorerControllerC163 iteration =
                NewController(new FakeSource());
            lifecycle &= iteration.TryRecomputeLayout(960, 526) &&
                iteration.TryRecomputeLayout(600, 360) &&
                iteration.Controls.RegistrationCount == 6 &&
                iteration.RowCount == 64;
        }
        StressLifecycleCycles = LifecycleCount;

        bool restored = controller.TryRecomputeLayout(baselineWidth, baselineHeight);
        StressDriftCount = restored && controller.WindowWidth == baselineWidth &&
            controller.WindowHeight == baselineHeight &&
            controller.Controls.RegistrationCount == baselineControlCount &&
            controller.Browser.CurrentPath ==
                GuideXosDirectoryBrowserC163.InitialPath &&
            source.LoadCalls > baselineLoads && source.StatCalls > baselineStats &&
            controller.List.FirstVisibleIndex <=
                controller.List.MaximumFirstVisibleIndex ? 0 : 1;
        bool result = layouts && selections && scrolling && keyboard && navigation &&
            lifecycle && StressLayoutAllocationBytes == 0 &&
            StressDriftCount == 0 && controller.Controls.RegistrationCount == 6 &&
            !controller.Controls.HasTransientInputCapture &&
            !controller.Controls.HasPointerDragCapture;
        StressFailureDetail = result ? "none" :
            $"layout={layouts} selection={selections} scroll={scrolling} keyboard={keyboard} navigation={navigation} lifecycle={lifecycle} allocation={StressLayoutAllocationBytes} drift={StressDriftCount}";
        host?.TryLog(result
            ? "C173-STRESS result=PASS"u8
            : "C173-STRESS result=FAIL"u8);
        return result;
    }

    private static GuideXosFileExplorerControllerC163 NewController(FakeSource source)
    {
        GuideXosFileExplorerControllerC163 controller =
            new(source);
        controller.InitializeControls();
        controller.TryRecomputeLayout(
            Applications.ManagedFileExplorerC163.SurfaceWidth,
            Applications.ManagedFileExplorerC163.SurfaceHeight);
        controller.OpenInitial();
        return controller;
    }

    private static int FindIndex(GuideXosDirectoryBrowserC163 browser,
        string name)
    {
        for (int index = 0; index < browser.EntryCount; index++)
            if (browser.EntryAt(index).Name == name) return index;
        return -1;
    }

    private static bool LayoutWithinClient(
        GuideXosFileExplorerControllerC163 controller)
    {
        int clientHeight = controller.WindowHeight -
            Applications.ManagedFileExplorerC163.NativeTitlebarHeight;
        return controller.ListXPosition >= 0 && controller.ListYPosition >= 0 &&
            controller.ListXPosition + controller.ListWidth <= controller.DetailX &&
            controller.ListYPosition + controller.ListHeight <= controller.StatusY &&
            controller.DetailX + 256 <= controller.WindowWidth &&
            controller.PathWidth <= controller.WindowWidth - 40 &&
            controller.StatusY + 18 <= clientHeight &&
            controller.ButtonYPosition + 28 <= clientHeight &&
            controller.FirstVisibleIndex >= 0 &&
            controller.FirstVisibleIndex <= controller.List.MaximumFirstVisibleIndex;
    }

    private static bool Case(ref int count, bool condition)
    {
        ++count;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = count;
        return condition;
    }
}
#endif
