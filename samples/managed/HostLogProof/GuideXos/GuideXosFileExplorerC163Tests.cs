#if HOSTLOGPROOF_C163_FILE_EXPLORER_PROOF
using System;
using System.Text;

namespace HostLogProof;

internal static class GuideXosManagedFileExplorerC163Tests
{
    private sealed class FakeSource : GuideXosFileExplorerSourceC163
    {
        public GuideXosDirectoryEntry[] RootEntries = DefaultRoot();
        public GuideXosDirectoryEntry[] ChildEntries =
        {
            new("Sub", GuideXosEntryType.Directory, 0u),
            new("deep.bin", GuideXosEntryType.Regular, 456u),
        };
        public GuideXosDirectoryEntry[] SubEntries =
        {
            new("nested.txt", GuideXosEntryType.Regular, 789u),
        };
        public GuideXosDirectoryEntry[] EmptyEntries = Array.Empty<GuideXosDirectoryEntry>();
        public bool RootHasMore;
        public string FailLoadPath;
        public GuideXosFileResult FailLoadResult = GuideXosFileResult.NotFound;
        public bool FailStat;
        public int LoadCalls;
        public int StatCalls;
        public int ActiveHandles;

        public override GuideXosFileResult Load(string path,
            out GuideXosDirectoryEntry[] entries, out bool hasMore)
        {
            LoadCalls++;
            ActiveHandles++;
            try
            {
                if (string.Equals(path, FailLoadPath, StringComparison.Ordinal))
                {
                    entries = Array.Empty<GuideXosDirectoryEntry>();
                    hasMore = false;
                    return FailLoadResult;
                }
                hasMore = false;
                if (path == GuideXosDirectoryBrowserC163.InitialPath)
                {
                    entries = RootEntries;
                    hasMore = RootHasMore;
                }
                else if (path == GuideXosDirectoryBrowserC163.InitialPath + "/Docs")
                    entries = ChildEntries;
                else if (path == GuideXosDirectoryBrowserC163.InitialPath + "/Docs/Sub")
                    entries = SubEntries;
                else if (path == GuideXosDirectoryBrowserC163.InitialPath + "/Empty")
                    entries = EmptyEntries;
                else
                {
                    entries = Array.Empty<GuideXosDirectoryEntry>();
                    return GuideXosFileResult.NotFound;
                }
                return GuideXosFileResult.Success;
            }
            finally
            {
                ActiveHandles--;
            }
        }

        public override GuideXosFileResult Stat(string path,
            out GuideXosFileInfo info)
        {
            StatCalls++;
            info = null;
            if (FailStat) return GuideXosFileResult.IoFailure;
            if (path.EndsWith("/alpha.txt", StringComparison.Ordinal) ||
                path.EndsWith("/deep.bin", StringComparison.Ordinal) ||
                path.EndsWith("/nested.txt", StringComparison.Ordinal))
            {
                ulong size = path.EndsWith("/alpha.txt", StringComparison.Ordinal)
                    ? 123u : path.EndsWith("/deep.bin", StringComparison.Ordinal)
                        ? 456u : 789u;
                info = new GuideXosFileInfo(GuideXosEntryType.Regular, size);
                return GuideXosFileResult.Success;
            }
            if (path.EndsWith("/Docs", StringComparison.Ordinal))
            {
                info = new GuideXosFileInfo(GuideXosEntryType.Directory, 0u);
                return GuideXosFileResult.Success;
            }
            return GuideXosFileResult.NotFound;
        }

        public static GuideXosDirectoryEntry[] DefaultRoot() => new[]
        {
            new GuideXosDirectoryEntry("z.txt", GuideXosEntryType.Regular, 44u),
            new GuideXosDirectoryEntry("alpha.txt", GuideXosEntryType.Regular, 123u),
            new GuideXosDirectoryEntry("Empty", GuideXosEntryType.Directory, 0u),
            new GuideXosDirectoryEntry("Docs", GuideXosEntryType.Directory, 0u),
        };
    }

    private static int s_cases;

    internal static bool Run(GuideXosHost host)
    {
        s_cases = 0;
        bool result = true;
        host?.TryLog("C163-FILE-EXPLORER-PROGRESS stage=begin"u8);

        FakeSource initialSource = new();
        GuideXosDirectoryBrowserC163 initial = NewBrowser(initialSource);
        result &= Case(initial.OpenInitial() == GuideXosFileResult.Success &&
            initial.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            initial.EntryCount == 4);
        result &= Case(initial.EntryAt(0).Name == "Docs" &&
            initial.EntryAt(1).Name == "Empty" &&
            initial.EntryAt(2).Name == "alpha.txt" &&
            initial.EntryAt(3).Name == "z.txt");
        result &= Case(initial.EntryAt(0).Type == GuideXosEntryType.Directory &&
            initial.EntryAt(2).Type == GuideXosEntryType.Regular);
        result &= Case(initial.Select(0) && initial.SelectedFileSize == null &&
            initialSource.StatCalls == 0);

        FakeSource emptySource = new();
        emptySource.RootEntries = Array.Empty<GuideXosDirectoryEntry>();
        GuideXosDirectoryBrowserC163 empty = NewBrowser(emptySource);
        result &= Case(empty.OpenInitial() == GuideXosFileResult.Success &&
            empty.EntryCount == 0 && empty.Status == "Directory is empty" &&
            !empty.HasSelection);

        GuideXosDirectoryBrowserC163 child = NewBrowser(new FakeSource());
        result &= Case(child.OpenInitial() == GuideXosFileResult.Success &&
            child.Select(0) &&
            child.OpenSelected() == GuideXosFileResult.Success &&
            child.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath + "/Docs" &&
            child.EntryCount == 2);
        result &= Case(child.NavigateUp() == GuideXosFileResult.Success &&
            child.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath);
        result &= Case(child.NavigateUp() == GuideXosFileResult.Success &&
            child.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            child.Status == "Already at the managed VFS root");

        FakeSource failedChildSource = new();
        failedChildSource.FailLoadPath =
            GuideXosDirectoryBrowserC163.InitialPath + "/Docs";
        GuideXosDirectoryBrowserC163 failedChild = NewBrowser(failedChildSource);
        bool failedChildTransactional =
            failedChild.OpenInitial() == GuideXosFileResult.Success &&
            failedChild.Select(0) &&
            failedChild.OpenSelected() == GuideXosFileResult.NotFound &&
            failedChild.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            failedChild.EntryCount == 4;
        result &= Case(failedChildTransactional);

        FakeSource missingCurrentSource = new();
        GuideXosDirectoryBrowserC163 missingCurrent = NewBrowser(missingCurrentSource);
        result &= Case(missingCurrent.OpenInitial() == GuideXosFileResult.Success &&
            missingCurrent.Select(0) &&
            missingCurrent.OpenSelected() == GuideXosFileResult.Success &&
            missingCurrent.CurrentPath.EndsWith("/Docs", StringComparison.Ordinal));
        missingCurrentSource.FailLoadPath = missingCurrent.CurrentPath;
        result &= Case(missingCurrent.Refresh() == GuideXosFileResult.NotFound &&
            missingCurrent.CurrentPath.EndsWith("/Docs", StringComparison.Ordinal) &&
            missingCurrent.EntryCount == 2 && missingCurrent.CanGoUp);
        missingCurrentSource.FailLoadPath = null;
        result &= Case(missingCurrent.NavigateUp() == GuideXosFileResult.Success &&
            missingCurrent.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath);

        FakeSource refreshSource = new();
        GuideXosDirectoryBrowserC163 refreshed = NewBrowser(refreshSource);
        result &= Case(refreshed.OpenInitial() == GuideXosFileResult.Success &&
            refreshed.Select(2) && refreshed.SelectedFileSize == 123u);
        int oldLoadCalls = refreshSource.LoadCalls;
        result &= Case(refreshed.Refresh() == GuideXosFileResult.Success &&
            refreshSource.LoadCalls == oldLoadCalls + 1 &&
            refreshed.SelectedEntry?.Name == "alpha.txt");
        result &= Case(refreshed.SelectedFileSize == 123u &&
            refreshSource.ActiveHandles == 0);

        refreshSource.FailLoadPath = GuideXosDirectoryBrowserC163.InitialPath;
        result &= Case(refreshed.Refresh() == GuideXosFileResult.NotFound &&
            refreshed.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            refreshed.EntryCount == 4 && refreshed.SelectedEntry?.Name == "alpha.txt");
        refreshSource.FailLoadPath = null;

        refreshSource.RootEntries = new[]
        {
            new GuideXosDirectoryEntry("z.txt", GuideXosEntryType.Regular, 44u),
            new GuideXosDirectoryEntry("Docs", GuideXosEntryType.Directory, 0u),
            new GuideXosDirectoryEntry("alpha.txt", GuideXosEntryType.Regular, 123u),
            new GuideXosDirectoryEntry("Empty", GuideXosEntryType.Directory, 0u),
        };
        result &= Case(refreshed.Refresh() == GuideXosFileResult.Success &&
            refreshed.SelectedEntry?.Name == "alpha.txt" &&
            refreshed.SelectedEntry.Type == GuideXosEntryType.Regular);

        FakeSource removedSource = new();
        GuideXosDirectoryBrowserC163 removed = NewBrowser(removedSource);
        result &= Case(removed.OpenInitial() == GuideXosFileResult.Success &&
            removed.Select(2));
        removedSource.RootEntries = new[]
        {
            new GuideXosDirectoryEntry("Docs", GuideXosEntryType.Directory, 0u),
            new GuideXosDirectoryEntry("Empty", GuideXosEntryType.Directory, 0u),
        };
        result &= Case(removed.Refresh() == GuideXosFileResult.Success &&
            !removed.HasSelection && removed.SelectedFileSize == null);

        FakeSource typeChangedSource = new();
        GuideXosDirectoryBrowserC163 typeChanged = NewBrowser(typeChangedSource);
        result &= Case(typeChanged.OpenInitial() == GuideXosFileResult.Success &&
            typeChanged.Select(0));
        typeChangedSource.RootEntries = new[]
        {
            new GuideXosDirectoryEntry("Docs", GuideXosEntryType.Regular, 8u),
        };
        result &= Case(typeChanged.Refresh() == GuideXosFileResult.Success &&
            !typeChanged.HasSelection);

        FakeSource fullSource = new();
        fullSource.RootEntries = CreateEntries(GuideXosDirectoryBrowserC163.EntryCapacity);
        GuideXosDirectoryBrowserC163 full = NewBrowser(fullSource);
        result &= Case(full.OpenInitial() == GuideXosFileResult.Success &&
            full.EntryCount == GuideXosDirectoryBrowserC163.EntryCapacity &&
            !full.HasMore);
        fullSource.RootHasMore = true;
        result &= Case(full.Refresh() == GuideXosFileResult.Success &&
            full.HasMore && full.Status.Contains("64", StringComparison.Ordinal) &&
            full.Status.Contains("more omitted", StringComparison.Ordinal));

        string maxName = new('x', 83);
        string maxPath;
        result &= Case(GuideXosPickerPath.TryBuildPath(
            GuideXosDirectoryBrowserC163.InitialPath, maxName, out maxPath) ==
                GuideXosPickerPathStatus.Success &&
            Encoding.UTF8.GetByteCount(maxPath) ==
                GuideXosDirectoryBrowserC163.PathCapacity);
        result &= Case(GuideXosPickerPath.TryBuildPath(
            GuideXosDirectoryBrowserC163.InitialPath, new string('x', 84),
            out _) == GuideXosPickerPathStatus.PathTooLong);
        result &= Case(GuideXosPickerPath.TryNormalizeDirectory(
            "/system/apps//Docs", out _) ==
                GuideXosPickerPathStatus.InvalidDirectory);
        result &= Case(GuideXosPickerPath.TryNormalizeDirectory(
            "/system/apps/./Docs", out _) ==
                GuideXosPickerPathStatus.InvalidDirectory);
        result &= Case(GuideXosPickerPath.TryNormalizeDirectory(
            "/system/apps/../Documents", out _) ==
                GuideXosPickerPathStatus.InvalidDirectory);

        FakeSource longNameSource = new();
        string fullName = new('L', 127);
        longNameSource.RootEntries = new[]
        {
            new GuideXosDirectoryEntry(fullName, GuideXosEntryType.Regular, 0u),
        };
        GuideXosFileExplorerControllerC163 longNameController =
            NewController(longNameSource);
        result &= Case(longNameController.List.GetItemLabel(0).Length ==
            GuideXosListBox.MaximumSupportedLabelLength &&
            longNameController.Browser.EntryAt(0).Name == fullName);

        FakeSource statFailureSource = new();
        statFailureSource.FailStat = true;
        GuideXosDirectoryBrowserC163 statFailure = NewBrowser(statFailureSource);
        result &= Case(statFailure.OpenInitial() == GuideXosFileResult.Success &&
            statFailure.Select(2) && statFailure.SelectedFileSize == null &&
            statFailure.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath);
        result &= Case(statFailure.OpenSelected() == GuideXosFileResult.InvalidArgument &&
            statFailure.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            statFailure.Status == "File activation requires the App Model host");

        FakeSource controllerSource = new();
        GuideXosFileExplorerControllerC163 controller =
            NewController(controllerSource);
        result &= Case(controller.ControlCount == 5 &&
            controller.ControlMaximum == 5 &&
            controller.SharedControlMaximum == 20 &&
            controller.Controls.ActiveControlId ==
                GuideXosFileExplorerControllerC163.ListControlId);
        controller.Browser.Select(0);
        controller.OpenSelected();
        result &= Case(controller.Controls.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosControlHostResult.Traversed &&
            controller.Controls.ActiveControlId ==
                GuideXosFileExplorerControllerC163.UpControlId);
        result &= Case(controller.Controls.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, shift: true)) ==
                GuideXosControlHostResult.Traversed &&
            controller.Controls.ActiveControlId ==
                GuideXosFileExplorerControllerC163.ListControlId);

        FakeSource enterSource = new();
        GuideXosFileExplorerControllerC163 enterController =
            NewController(enterSource);
        enterController.List.SelectIndex(0);
        enterController.Browser.Select(0);
        enterController.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        result &= Case(enterController.Browser.CurrentPath.EndsWith(
            "/Docs", StringComparison.Ordinal));

        FakeSource enterFileSource = new();
        GuideXosFileExplorerControllerC163 enterFileController =
            NewController(enterFileSource);
        enterFileController.List.SelectIndex(2);
        enterFileController.Browser.Select(2);
        enterFileController.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        result &= Case(enterFileController.Browser.CurrentPath ==
                GuideXosDirectoryBrowserC163.InitialPath &&
            enterFileController.Browser.Status ==
                "File activation unavailable");

        FakeSource shortcutSource = new();
        GuideXosFileExplorerControllerC163 shortcutController =
            NewController(shortcutSource);
        int beforeCtrlR = shortcutSource.LoadCalls;
        result &= Case(shortcutController.RouteInput(
            GuideXosInputEvent.ForKeyDown((GuideXosTextInputKey)'r',
                control: true)) == GuideXosFileExplorerCommandC163.Refresh &&
            shortcutSource.LoadCalls == beforeCtrlR + 1);
        shortcutController.RouteInput(GuideXosInputEvent.ForKeyChar('r'));
        result &= Case(shortcutSource.LoadCalls == beforeCtrlR + 1);

        FakeSource pointerSource = new();
        GuideXosFileExplorerControllerC163 pointerController =
            NewController(pointerSource);
        pointerController.RouteInput(GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
            GuideXosFileExplorerControllerC163.ListX + 8,
            GuideXosFileExplorerControllerC163.ListY + 18));
        result &= Case(pointerController.Browser.SelectedIndex == 1 &&
            pointerController.Browser.SelectedEntry?.Name == "Empty");

        FakeSource viewportSource = new();
        viewportSource.RootEntries = CreateEntries(24);
        GuideXosFileExplorerControllerC163 viewportController =
            NewController(viewportSource);
        viewportController.List.SelectIndex(5);
        viewportController.Browser.Select(5);
        viewportController.List.SetFirstVisibleIndex(2);
        result &= Case(viewportController.Refresh() == GuideXosFileResult.Success &&
            viewportController.Browser.SelectedIndex == 5 &&
            viewportController.FirstVisibleIndex == 2);
        viewportSource.RootEntries = Reordered(viewportSource.RootEntries);
        result &= Case(viewportController.Refresh() == GuideXosFileResult.Success &&
            viewportController.Browser.SelectedEntry?.Name == "entry-15" &&
            viewportController.FirstVisibleIndex >= 0 &&
            viewportController.FirstVisibleIndex <=
                viewportController.List.MaximumFirstVisibleIndex);

        FakeSource shiftedViewportSource = new();
        shiftedViewportSource.RootEntries = CreateEntries(24);
        GuideXosFileExplorerControllerC163 shiftedViewportController =
            NewController(shiftedViewportSource);
        shiftedViewportController.List.SelectIndex(8);
        shiftedViewportController.Browser.Select(8);
        shiftedViewportController.List.SetFirstVisibleIndex(0);
        GuideXosDirectoryEntry[] shifted = new GuideXosDirectoryEntry[32];
        for (int index = 0; index < 8; index++)
            shifted[index] = new GuideXosDirectoryEntry("AAA-" + index,
                GuideXosEntryType.Directory, 0u);
        Array.Copy(shiftedViewportSource.RootEntries, 0, shifted, 8, 24);
        shiftedViewportSource.RootEntries = shifted;
        result &= Case(shiftedViewportController.Refresh() == GuideXosFileResult.Success &&
            shiftedViewportController.Browser.SelectedEntry?.Name == "entry-01" &&
            shiftedViewportController.FirstVisibleIndex > 0 &&
            shiftedViewportController.Browser.SelectedIndex >=
                shiftedViewportController.FirstVisibleIndex &&
            shiftedViewportController.Browser.SelectedIndex <
                shiftedViewportController.FirstVisibleIndex +
                    shiftedViewportController.List.VisibleRowCount);

        FakeSource wheelSource = new();
        wheelSource.RootEntries = CreateEntries(24);
        GuideXosFileExplorerControllerC163 wheelController =
            NewController(wheelSource);
        wheelController.List.SetFirstVisibleIndex(4);
        int initialViewport = wheelController.FirstVisibleIndex;
        wheelController.RouteInput(GuideXosInputEvent.ForWheel(
            GuideXosFileExplorerControllerC163.ListX + 16,
            GuideXosFileExplorerControllerC163.ListY + 18, 1));
        result &= Case(wheelController.FirstVisibleIndex != initialViewport &&
            wheelController.List.SelectedIndex == -1 &&
            !wheelController.Browser.HasSelection);

        FakeSource stressSource = new();
        GuideXosDirectoryBrowserC163 stress = NewBrowser(stressSource);
        result &= Case(stress.OpenInitial() == GuideXosFileResult.Success);
        for (int index = 0; index < 1000; index++)
            result &= stress.Refresh() == GuideXosFileResult.Success;
        result &= Case(stress.EntryCount == 4 && stressSource.ActiveHandles == 0 &&
            stressSource.LoadCalls == 1001);

        FakeSource navigationSource = new();
        GuideXosDirectoryBrowserC163 navigation = NewBrowser(navigationSource);
        result &= Case(navigation.OpenInitial() == GuideXosFileResult.Success);
        for (int index = 0; index < 100; index++)
        {
            navigation.Select(0);
            result &= navigation.OpenSelected() == GuideXosFileResult.Success;
            result &= navigation.Refresh() == GuideXosFileResult.Success;
            result &= navigation.NavigateUp() == GuideXosFileResult.Success;
        }
        result &= Case(navigation.CurrentPath ==
            GuideXosDirectoryBrowserC163.InitialPath &&
            navigation.EntryCount == 4 && navigationSource.ActiveHandles == 0);

        GuideXosDirectoryBrowserC163 relaunched = NewBrowser(new FakeSource());
        result &= Case(relaunched.OpenInitial() == GuideXosFileResult.Success &&
            relaunched.CurrentPath == GuideXosDirectoryBrowserC163.InitialPath &&
            !relaunched.HasSelection);

        Span<byte> summary = stackalloc byte[128];
        int position = 0;
        GuideXosText.Append(summary, ref position,
            "C163-FILE-EXPLORER-TESTS cases="u8);
        GuideXosText.AppendUnsigned(summary, ref position, (uint)s_cases);
        GuideXosText.Append(summary, ref position,
            " refresh=1000 navigation=100 bounded="u8);
        GuideXosText.Append(summary, ref position,
            result && s_cases >= 25 ? "true"u8 : "false"u8);
        GuideXosText.Append(summary, ref position,
            result && s_cases >= 25 ? " result=PASS"u8 : " result=FAIL"u8);
        host?.TryLog(summary[..position]);
        return result && s_cases >= 25;
    }

    private static GuideXosDirectoryBrowserC163 NewBrowser(FakeSource source) =>
        new(source);

    private static GuideXosFileExplorerControllerC163 NewController(
        FakeSource source)
    {
        GuideXosFileExplorerControllerC163 controller = new(source);
        controller.InitializeControls();
        controller.OpenInitial();
        return controller;
    }

    private static GuideXosDirectoryEntry[] CreateEntries(int count)
    {
        GuideXosDirectoryEntry[] entries =
            new GuideXosDirectoryEntry[count];
        for (int index = 0; index < count; index++)
        {
            entries[index] = new GuideXosDirectoryEntry(
                "entry-" + index.ToString("D2"),
                index % 3 == 0 ? GuideXosEntryType.Directory :
                    GuideXosEntryType.Regular, (ulong)index);
        }
        return entries;
    }

    private static GuideXosDirectoryEntry[] Reordered(
        GuideXosDirectoryEntry[] entries)
    {
        GuideXosDirectoryEntry[] reordered = new GuideXosDirectoryEntry[entries.Length];
        for (int index = 0; index < entries.Length; index++)
            reordered[index] = entries[entries.Length - index - 1];
        return reordered;
    }

    private static bool Case(bool condition)
    {
        s_cases++;
        return condition;
    }
}
#endif
