#if HOSTLOGPROOF_C161_TASK_MANAGER_PROOF
using System;

namespace HostLogProof;

internal static unsafe class GuideXosManagedTaskManagerC161Tests
{
    private const string CalculatorId =
        "com.guidexos.apps.managed.calculator";
    private const string TaskManagerId =
        "com.guidexos.apps.managed.taskmanager";

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        passed &= Case(ref cases, TestInitialAndDetails());
        passed &= TestCloseEligibility(ref cases);
        passed &= Case(ref cases, TestEmptyAndMaximum());
        passed &= Case(ref cases, TestTruncation());
        passed &= Case(ref cases, TestIdentityAndReorder());
        passed &= Case(ref cases, TestDisappearanceAndReuse());
        passed &= Case(ref cases, TestSelectionDetailsAndText());
        passed &= Case(ref cases, TestInputAndFocus());
        passed &= Case(ref cases, TestViewportAndWheel());
        passed &= Case(ref cases, TestRefreshViewportPreserved());
        passed &= Case(ref cases, TestFailuresAndRecovery());
        passed &= Case(ref cases, TestRegistrationAndSources());
        passed &= Case(ref cases, TestPointerSelection());
        passed &= Case(ref cases, TestRefreshButton());
        passed &= Case(ref cases, TestKeyboardSelection());
        passed &= Case(ref cases, TestHomeEndAndEnter());
        passed &= Case(ref cases, TestNoFakeMetrics());
        passed &= Case(ref cases, TestLongApplicationIdSplit());
        passed &= Case(ref cases, TestFullWidthLifetime());
        passed &= Case(ref cases, TestOrderPreserved());
        passed &= Case(ref cases, TestSelectionStaysEmpty());
        passed &= Case(ref cases, TestCloseCommand());
        passed &= Case(ref cases, TestUnmodifiedRDoesNothing());
        passed &= Case(ref cases, TestInactiveRecord());
        passed &= StressWrapperAndRows(host, ref cases);

        Span<byte> line = stackalloc byte[112];
        int position = 0;
        GuideXosText.Append(line, ref position, "C161-TM-TESTS cases="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)cases);
        GuideXosText.Append(line, ref position,
            " initial=PASS selection=identity stress=1000 result="u8);
        GuideXosText.Append(line, ref position,
            passed ? "PASS"u8 : "FAIL"u8);
        host?.TryLog(line[..position]);
        return passed && cases >= 20;
    }

    private static bool TestInitialAndDetails()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false, (3, 0x100UL, TaskManagerId,
                "Managed Task Manager", true,
                GuideXosApplicationSnapshotState.Running)));
        return controller.RowCount == 1 && controller.SelectedIndex == 0 &&
            controller.HasSelection &&
            !controller.CloseApplicationButton.EffectiveEnabled &&
            controller.SelectedIdentity.Value == 0x100UL &&
            controller.SelectedIdentity.Source ==
                GuideXosApplicationSnapshotSource.ManagedLogicalApplication &&
            controller.SelectedNameText == "Name: Managed Task Manager" &&
            controller.SelectedApplicationIdText == "App ID: " + TaskManagerId &&
            controller.SelectedLifetimeText == "Lifetime: 0x0000000000000100" &&
            controller.SelectedStateText == "State: Running" &&
            controller.SelectedActiveText == "Active: Yes" &&
            controller.SelectedSourceText == "Source: Managed app";
    }

    private static bool TestCloseEligibility(ref int cases)
    {
        GuideXosTaskManagerControllerC161 none = ReadyController(
            CreateSnapshot(0, 0, false));
        bool passed = Case(ref cases,
            !none.CloseApplicationButton.EffectiveEnabled);
        GuideXosTaskManagerControllerC161 native = ReadyController(
            CreateSnapshot(1, 1, false,
                (1, 10UL, "gxos.builtin.calculator", "Calculator", false,
                    GuideXosApplicationSnapshotState.Running)));
        passed &= Case(ref cases,
            native.CloseApplicationButton.EffectiveEnabled);
        GuideXosTaskManagerControllerC161 shell = ReadyController(
            CreateSnapshot(1, 1, false,
                (2, 11UL, "", "Terminal", false,
                    GuideXosApplicationSnapshotState.Running)));
        passed &= Case(ref cases,
            !shell.CloseApplicationButton.EffectiveEnabled);
        GuideXosTaskManagerControllerC161 calculator = ReadyController(
            CreateSnapshot(1, 1, false,
                (3, 12UL, CalculatorId, "Managed Calculator", false,
                    GuideXosApplicationSnapshotState.Running)));
        passed &= Case(ref cases,
            calculator.CloseApplicationButton.EffectiveEnabled);
        GuideXosTaskManagerControllerC161 notes = ReadyController(
            CreateSnapshot(1, 1, false,
                (3, 13UL, "com.guidexos.apps.managed.notes", "Notes", false,
                    GuideXosApplicationSnapshotState.Running)));
        passed &= Case(ref cases,
            !notes.CloseApplicationButton.EffectiveEnabled);
        GuideXosTaskManagerControllerC161 terminated = ReadyController(
            CreateSnapshot(1, 1, false,
                (1, 14UL, "gxos.builtin.calculator", "Calculator", false,
                    GuideXosApplicationSnapshotState.Terminated)));
        passed &= Case(ref cases,
            !terminated.CloseApplicationButton.EffectiveEnabled);
        return passed;
    }

    private static bool TestEmptyAndMaximum()
    {
        GuideXosTaskManagerControllerC161 empty = ReadyController(
            CreateSnapshot(0, 0, false));
        bool emptyPass = empty.RowCount == 0 && !empty.HasSelection &&
            empty.SelectedIndex == -1 &&
            empty.StatusText == "No applications in snapshot";

        GuideXosApplicationSnapshot maximum = CreateSnapshot(
            GuideXosTaskManagerControllerC161.ApplicationCapacity,
            GuideXosTaskManagerControllerC161.ApplicationCapacity, false);
        GuideXosTaskManagerControllerC161 full = ReadyController(maximum);
        return emptyPass && full.RowCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity &&
            full.List.MaximumItemCount ==
                GxAbi.ApplicationSnapshotCapacity &&
            full.List.RejectedOperationCount == 0u;
    }

    private static bool TestTruncation()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(17, 18, true));
        return controller.RowCount == 17 &&
            controller.LastResult == GuideXosApplicationSnapshotResult.Truncated &&
            controller.StatusText == "Showing 17 of 18 applications";
    }

    private static bool TestIdentityAndReorder()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(3, 3, false,
                (1, 100UL, "gxos.test.a", "Native A", false,
                    GuideXosApplicationSnapshotState.Running),
                (1, 101UL, "gxos.test.b", "Native B", true,
                    GuideXosApplicationSnapshotState.Running),
                (3, 100UL, CalculatorId, "Managed Calculator", false,
                    GuideXosApplicationSnapshotState.Running)));
        controller.List.SelectIndex(1);
        controller.SyncSelectionFromList();
        GuideXosApplicationInstanceId selected = controller.SelectedIdentity;
        bool identityIncludesSource = selected.Source ==
            GuideXosApplicationSnapshotSource.AppManagerInstance &&
            selected.Value == 101UL;

        GuideXosApplicationSnapshot reordered = CreateSnapshot(3, 3, false,
            (3, 100UL, CalculatorId, "Managed Calculator", false,
                GuideXosApplicationSnapshotState.Running),
            (1, 100UL, "gxos.test.a", "Native A", false,
                GuideXosApplicationSnapshotState.Running),
            (1, 101UL, "gxos.test.b", "Native B", true,
                GuideXosApplicationSnapshotState.Running));
        bool applied = controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in reordered);
        return identityIncludesSource && applied &&
            controller.SelectedIndex == 2 && controller.HasSelection &&
            controller.SelectedIdentity == selected &&
            controller.SelectedNameText == "Name: Native B";
    }

    private static bool TestDisappearanceAndReuse()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(2, 2, false,
                (1, 100UL, "gxos.test.old", "Old instance", false,
                    GuideXosApplicationSnapshotState.Running),
                (1, 200UL, "gxos.test.other", "Other", true,
                    GuideXosApplicationSnapshotState.Running)));
        controller.List.SelectIndex(0);
        controller.SyncSelectionFromList();
        GuideXosApplicationSnapshot replacement = CreateSnapshot(1, 1, false,
            (1, 101UL, "gxos.test.new", "Replacement", true,
                GuideXosApplicationSnapshotState.Running));
        bool applied = controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in replacement);
        return applied && controller.RowCount == 1 && !controller.HasSelection &&
            controller.SelectedIndex == -1 &&
            controller.SelectedNameText.Length == 0 &&
            controller.StatusText == "Showing 1 applications" &&
            controller.FirstVisibleIndex == 0;
    }

    private static bool TestSelectionDetailsAndText()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(2, 2, false,
                (3, 1UL, CalculatorId, "Managed Calculator", false,
                    GuideXosApplicationSnapshotState.Running),
                (1, 2UL, "gxos.builtin.calculator", "Calculator", true,
                    GuideXosApplicationSnapshotState.Suspended)));
        bool compactRow = !controller.List.GetItemLabel(0).Contains(CalculatorId,
            StringComparison.Ordinal) &&
            controller.List.GetItemLabel(0).Contains("Managed Calculator",
                StringComparison.Ordinal);
        controller.List.SelectIndex(1);
        controller.SyncSelectionFromList();
        bool selectedDetails = controller.SelectedNameText == "Name: Calculator" &&
            controller.SelectedApplicationIdText ==
                "App ID: gxos.builtin.calculator" &&
            controller.SelectedStateText == "State: Suspended" &&
            controller.SelectedActiveText == "Active: Yes" &&
            controller.SelectedSourceText == "Source: AppManager";
        return compactRow && selectedDetails;
    }

    private static bool TestInputAndFocus()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        GuideXosTaskManagerCommandC161 enterOnRow = controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        bool noRowAction = enterOnRow == GuideXosTaskManagerCommandC161.None;
        bool tabToRefresh = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.RefreshControlId;
        bool shiftTabToList = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.ListControlId;
        bool tabToClose = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosTaskManagerCommandC161.None &&
                controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.CloseApplicationControlId;
        bool shiftTabFromCloseToRefresh = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.RefreshControlId;
        bool shiftTabFromRefreshToList = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.ListControlId;
        bool shiftWrapToClose = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.CloseControlId;
        bool tabWrapToList = controller.RouteInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.ListControlId;

        GuideXosTaskManagerCommandC161 ctrlR = controller.RouteInput(
            GuideXosInputEvent.ForKeyDown((GuideXosTextInputKey)'r', control: true));
        GuideXosTaskManagerCommandC161 splitChar = controller.RouteInput(
            GuideXosInputEvent.ForKeyChar('r'));
        return noRowAction && tabToRefresh && shiftTabToList && tabToClose &&
            shiftTabFromCloseToRefresh && shiftTabFromRefreshToList &&
            shiftWrapToClose && tabWrapToList &&
            ctrlR == GuideXosTaskManagerCommandC161.Refresh &&
            splitChar == GuideXosTaskManagerCommandC161.None &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.ListControlId;
    }

    private static bool TestViewportAndWheel()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        controller.List.SelectIndex(17);
        controller.SyncSelectionFromList();
        bool selectedAtEnd = controller.FirstVisibleIndex == 8;

        bool natural = false;
#if HOSTLOGPROOF_C147_RUNTIME_SETTINGS
        natural = Applications.GuideXosRuntimeSettings.Current.NaturalScroll;
#endif
        int wheelDelta = natural ? 1 : -1;
        controller.List.SetFirstVisibleIndex(3);
        controller.RouteInput(GuideXosInputEvent.ForWheel(
            GuideXosTaskManagerControllerC161.ListX + 12,
            GuideXosTaskManagerControllerC161.ListY + 12, wheelDelta));
        bool wheelUsesListViewport = controller.FirstVisibleIndex != 3;

        GuideXosApplicationSnapshot smaller = CreateSnapshot(2, 2, false);
        controller.ApplySnapshot(GuideXosApplicationSnapshotResult.Success,
            in smaller);
        return selectedAtEnd && wheelUsesListViewport &&
            controller.FirstVisibleIndex == 0 &&
            controller.FirstVisibleIndex <= controller.List.MaximumFirstVisibleIndex;
    }

    private static bool TestRefreshViewportPreserved()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        controller.List.SetFirstVisibleIndex(3);
        controller.List.SelectIndex(6);
        controller.SyncSelectionFromList();
        GuideXosApplicationInstanceId selected = controller.SelectedIdentity;
        GuideXosApplicationSnapshot refreshed = CreateSnapshot(18, 18, false);
        bool applied = controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in refreshed);
        return applied && controller.FirstVisibleIndex == 3 &&
            controller.SelectedIndex == 6 &&
            controller.SelectedIdentity == selected &&
            controller.TryGetSelectedRecord(out _, out int index) && index == 6;
    }

    private static bool TestFailuresAndRecovery()
    {
        GuideXosTaskManagerControllerC161 firstFailure = new();
        bool supportedFailure = !firstFailure.ApplySnapshot(
                GuideXosApplicationSnapshotResult.NotSupported, default) &&
            !firstFailure.HasSnapshot && firstFailure.RowCount == 0 &&
            firstFailure.StatusText.Contains("ABI v2", StringComparison.Ordinal);

        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(2, 2, false,
                (1, 100UL, "gxos.test.a", "A", false,
                    GuideXosApplicationSnapshotState.Running),
                (1, 200UL, "gxos.test.b", "B", true,
                    GuideXosApplicationSnapshotState.Running)));
        controller.List.SelectIndex(1);
        controller.SyncSelectionFromList();
        GuideXosApplicationInstanceId selected = controller.SelectedIdentity;
        GuideXosApplicationSnapshot malformed = default;
        malformed.totalCount = 1u;
        malformed.copiedCount = 1u;
        malformed.nativeResult = 0;
        bool invalidPreserved = !controller.ApplySnapshot(
                GuideXosApplicationSnapshotResult.Success, in malformed) &&
            controller.RowCount == 2 && controller.HasSelection &&
            controller.SelectedIdentity == selected &&
            controller.LastResult ==
                GuideXosApplicationSnapshotResult.InvalidData &&
            controller.StatusText.Contains("invalid data",
                StringComparison.OrdinalIgnoreCase);
        bool preservedFailure = !controller.ApplySnapshot(
                GuideXosApplicationSnapshotResult.NativeFailure, default) &&
            controller.RowCount == 2 && controller.HasSelection &&
            controller.SelectedIdentity == selected &&
            controller.StatusText.Contains("native failure",
                StringComparison.OrdinalIgnoreCase);
        GuideXosApplicationSnapshot recovered = CreateSnapshot(2, 2, false,
            (1, 100UL, "gxos.test.a", "A", false,
                GuideXosApplicationSnapshotState.Running),
            (1, 200UL, "gxos.test.b", "B", true,
                GuideXosApplicationSnapshotState.Running));
        bool recoveredResult = controller.ApplySnapshot(
            GuideXosApplicationSnapshotResult.Success, in recovered);
        return supportedFailure && invalidPreserved && preservedFailure && recoveredResult &&
            controller.SelectedIdentity == selected &&
            controller.StatusText == "Showing 2 applications";
    }

    private static bool TestRegistrationAndSources()
    {
        GuideXosTaskManagerControllerC161 controller = new();
        int expectedRegistryEntries = 4;
#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
        expectedRegistryEntries = 5;
#endif
        bool registered = controller.InitializeControls() &&
            controller.ControlCount == 4 && controller.ControlMaximum == 4 &&
            GuideXosApplicationRegistry.RegistrationCount == expectedRegistryEntries &&
            controller.Controls.MaximumControlCount == 4 &&
            controller.SharedControlMaximum ==
                GuideXosControlHost.MaximumSupportedControlCount &&
            controller.List.MaximumItemCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity;
        GuideXosTaskManagerControllerC161 sources = ReadyController(
            CreateSnapshot(3, 3, false,
                (1, 1UL, "gxos.builtin.calculator", "Calculator", false,
                    GuideXosApplicationSnapshotState.Running),
                (2, 1UL, "", "Terminal", false,
                    GuideXosApplicationSnapshotState.Running),
                (3, 1UL, CalculatorId, "Managed Calculator", true,
                    GuideXosApplicationSnapshotState.Running)));
        bool sourcesDistinct = sources.List.GetItemLabel(0).Contains("Calculator",
                StringComparison.Ordinal) &&
            sources.List.GetItemLabel(1).Contains("Terminal",
                StringComparison.Ordinal) &&
            sources.List.GetItemLabel(2).Contains("Managed Calculator",
                StringComparison.Ordinal) &&
            sources.List.MaximumItemCount ==
                GuideXosTaskManagerControllerC161.ApplicationCapacity;
        sources.List.SelectIndex(1);
        sources.SyncSelectionFromList();
        bool shellHasNoId = sources.SelectedSourceText == "Source: Shell surface" &&
            sources.SelectedApplicationIdText == "App ID: unavailable";
        return registered && sourcesDistinct && shellHasNoId;
    }

    private static bool TestPointerSelection()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        GuideXosTaskManagerCommandC161 command = controller.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                GuideXosTaskManagerControllerC161.ListX + 16,
                GuideXosTaskManagerControllerC161.ListY + 5 * 18 + 4));
        return command == GuideXosTaskManagerCommandC161.None &&
            controller.SelectedIndex == 5 && controller.HasSelection &&
            controller.SelectedIdentity.Value == 1005UL &&
            controller.SelectedNameText == "Name: Test application";
    }

    private static bool TestRefreshButton()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false));
        GuideXosTaskManagerCommandC161 command = controller.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                GuideXosTaskManagerControllerC161.RefreshX + 8,
                GuideXosTaskManagerControllerC161.ButtonY + 8));
        return command == GuideXosTaskManagerCommandC161.Refresh &&
            controller.Controls.ActiveControlId ==
                GuideXosTaskManagerControllerC161.RefreshControlId;
    }

    private static bool TestKeyboardSelection()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down));
        bool down = controller.SelectedIndex == 1 &&
            controller.SelectedIdentity.Value == 1001UL;
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Up));
        return down && controller.SelectedIndex == 0 &&
            controller.SelectedIdentity.Value == 1000UL;
    }

    private static bool TestHomeEndAndEnter()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(18, 18, false));
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.End));
        bool end = controller.SelectedIndex == 17 &&
            controller.FirstVisibleIndex == 8;
        controller.RouteInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Home));
        GuideXosTaskManagerCommandC161 enter = controller.RouteInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter));
        return end && controller.SelectedIndex == 0 &&
            enter == GuideXosTaskManagerCommandC161.None;
    }

    private static bool TestNoFakeMetrics()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false));
        string row = controller.List.GetItemLabel(0);
        return !row.Contains("CPU", StringComparison.OrdinalIgnoreCase) &&
            !row.Contains("memory", StringComparison.OrdinalIgnoreCase) &&
            !row.Contains('%');
    }

    private static bool TestLongApplicationIdSplit()
    {
        string longId = "com.guidexos.apps.managed." + new string('x', 60);
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false,
                (3, 77UL, longId, "Long ID app", true,
                    GuideXosApplicationSnapshotState.Running)));
        return controller.SelectedApplicationIdText == "App ID: " + longId;
    }

    private static bool TestFullWidthLifetime()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false,
                (3, 0xFEDCBA9876543210UL, TaskManagerId,
                    "Managed Task Manager", true,
                    GuideXosApplicationSnapshotState.Running)));
        return controller.SelectedLifetimeText ==
            "Lifetime: 0xFEDCBA9876543210" &&
            controller.SelectedIdentity.Value == 0xFEDCBA9876543210UL;
    }

    private static bool TestOrderPreserved()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(3, 3, false,
                (1, 1UL, "gxos.test.first", "First", false,
                    GuideXosApplicationSnapshotState.Running),
                (2, 2UL, "", "Terminal", false,
                    GuideXosApplicationSnapshotState.Running),
                (3, 3UL, CalculatorId, "Managed Calculator", true,
                    GuideXosApplicationSnapshotState.Running)));
        return controller.List.GetItemLabel(0).Contains("First",
                StringComparison.Ordinal) &&
            controller.List.GetItemLabel(1).Contains("Terminal",
                StringComparison.Ordinal) &&
            controller.List.GetItemLabel(2).Contains("Managed Calculator",
                StringComparison.Ordinal);
    }

    private static bool TestSelectionStaysEmpty()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false,
                (1, 100UL, "gxos.test.old", "Old", false,
                    GuideXosApplicationSnapshotState.Running)));
        GuideXosApplicationSnapshot replacement = CreateSnapshot(1, 1, false,
            (1, 101UL, "gxos.test.new", "New", false,
                GuideXosApplicationSnapshotState.Running));
        controller.ApplySnapshot(GuideXosApplicationSnapshotResult.Success,
            in replacement);
        GuideXosApplicationSnapshot next = CreateSnapshot(1, 1, false,
            (1, 102UL, "gxos.test.next", "Next", false,
                GuideXosApplicationSnapshotState.Running));
        controller.ApplySnapshot(GuideXosApplicationSnapshotResult.Success,
            in next);
        return controller.RowCount == 1 && !controller.HasSelection &&
            controller.SelectedIndex == -1;
    }

    private static bool TestCloseCommand()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false));
        GuideXosTaskManagerCommandC161 command = controller.RouteInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary,
                GuideXosTaskManagerControllerC161.CloseX + 8,
                GuideXosTaskManagerControllerC161.ButtonY + 8));
        return command == GuideXosTaskManagerCommandC161.Close &&
            controller.HasSnapshot;
    }

    private static bool TestUnmodifiedRDoesNothing()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false));
        GuideXosApplicationInstanceId before = controller.SelectedIdentity;
        GuideXosTaskManagerCommandC161 command = controller.RouteInput(
            GuideXosInputEvent.ForKeyChar('r'));
        return command == GuideXosTaskManagerCommandC161.None &&
            controller.SelectedIdentity == before && controller.RowCount == 1;
    }

    private static bool TestInactiveRecord()
    {
        GuideXosTaskManagerControllerC161 controller = ReadyController(
            CreateSnapshot(1, 1, false,
                (1, 55UL, "gxos.test.inactive", "Inactive", false,
                    GuideXosApplicationSnapshotState.Running)));
        return controller.SelectedActiveText == "Active: No" &&
            controller.SelectedStateText == "State: Running";
    }

    private static bool StressWrapperAndRows(GuideXosHost host, ref int cases)
    {
        if (host == null) return Case(ref cases, false);
        GuideXosTaskManagerControllerC161 controller = new();
        bool passed = controller.InitializeControls();
        GuideXosApplicationSnapshotResult firstResult =
            host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot first);
        passed &= controller.ApplySnapshot(firstResult, in first);
        GuideXosApplicationInstanceId initial = controller.SelectedIdentity;
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            GuideXosApplicationSnapshotResult result =
                host.TryGetApplicationSnapshot(out GuideXosApplicationSnapshot snapshot);
            if (result != GuideXosApplicationSnapshotResult.Success &&
                result != GuideXosApplicationSnapshotResult.Truncated)
            {
                passed = false;
                break;
            }
            passed &= controller.ApplySnapshot(result, in snapshot);
            passed &= controller.RowCount <=
                GuideXosTaskManagerControllerC161.ApplicationCapacity;
            if (iteration == 0 || iteration == 999)
                passed &= controller.SelectedIdentity == initial;
        }
        passed &= controller.ControlCount == 4 &&
            controller.List.RejectedOperationCount == 0u;
        return Case(ref cases, passed);
    }

    private static GuideXosTaskManagerControllerC161 ReadyController(
        GuideXosApplicationSnapshot snapshot)
    {
        GuideXosTaskManagerControllerC161 controller = new();
        controller.InitializeControls();
        controller.ApplySnapshot(
            snapshot.IsTruncated
                ? GuideXosApplicationSnapshotResult.Truncated
                : GuideXosApplicationSnapshotResult.Success,
            in snapshot);
        return controller;
    }

    private static GuideXosApplicationSnapshot CreateSnapshot(
        uint count, uint totalCount, bool truncated,
        params (uint source, ulong id, string appId, string name,
            bool active, GuideXosApplicationSnapshotState state)[] records)
    {
        GuideXosApplicationSnapshot snapshot = default;
        snapshot.totalCount = totalCount;
        snapshot.copiedCount = count;
        snapshot.nativeResult = truncated ? 1 : 0;
        for (uint index = 0; index < count; ++index)
        {
            (uint source, ulong id, string appId, string name,
                bool active, GuideXosApplicationSnapshotState state) =
                index < (uint)records.Length
                    ? records[(int)index]
                    : ((uint)GuideXosApplicationSnapshotSource.AppManagerInstance,
                        1000UL + index, "gxos.test.app", "Test application",
                        false, GuideXosApplicationSnapshotState.Running);
            GuideXosApplicationSnapshotRecord record = default;
            record.recordVersion = GxAbi.ApplicationSnapshotRecordVersion;
            record.source = (GuideXosApplicationSnapshotSource)source;
            record.instanceId = id;
            record.state = state;
            record.flags = active ? 1u : 0u;
            record.displayNameLength = (uint)name.Length;
            record.applicationIdLength = (uint)appId.Length;
            byte* targetName = record.displayName;
            for (int character = 0; character < name.Length; ++character)
                targetName[character] = (byte)name[character];
            targetName[name.Length] = 0;
            byte* targetId = record.applicationId;
            for (int character = 0; character < appId.Length; ++character)
                targetId[character] = (byte)appId[character];
            targetId[appId.Length] = 0;
            ulong* storage = snapshot.recordStorage;
            *(GuideXosApplicationSnapshotRecord*)((byte*)storage +
                (int)(index * GxAbi.ApplicationSnapshotRecordSize)) = record;
        }
        return snapshot;
    }

    private static bool Case(ref int cases, bool passed)
    {
        ++cases;
        return passed;
    }
}
#endif
