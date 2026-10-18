using HostLogProof;
using HostLogProof.Applications;

int passedSuites = 0;
int failedSuites = 0;
int totalCases = 0;

bool RunSuite(string name, int cases, Func<bool> run)
{
    bool passed;
    try
    {
        passed = run();
    }
    catch (Exception exception)
    {
        Console.WriteLine($"MANAGED-UI-SUITE name={name} cases={cases} result=FAIL exception={exception.GetType().Name}");
        Console.WriteLine(exception);
        failedSuites++;
        totalCases += cases;
        return false;
    }

    Console.WriteLine($"MANAGED-UI-SUITE name={name} cases={cases} result={(passed ? "PASS" : "FAIL")}");
    if (passed) passedSuites++;
    else failedSuites++;
    totalCases += cases;
    return passed;
}

bool allPassed = true;
allPassed &= RunSuite("C133-ComboBox", 44,
    () => GuideXosComboBoxC133Tests.Run(null));
allPassed &= RunSuite("C134-TransientRouting", 30,
    () => GuideXosComboBoxC134HostTests.Run(null));
allPassed &= RunSuite("C135-PopupMenuApi", 40,
    () => GuideXosPopupMenuC135Tests.Run(null));
allPassed &= RunSuite("C135-PopupMenuHost", 40,
    () => GuideXosPopupMenuC135HostTests.Run(null));
allPassed &= RunSuite("C137-Wheel", GuideXosMouseWheelC137Tests.TotalCaseCount,
    () => GuideXosMouseWheelC137Tests.Run(null));
allPassed &= RunSuite("C139-SharedViewport", GuideXosSharedScrollViewportC139Tests.TotalCaseCount,
    () => GuideXosSharedScrollViewportC139Tests.Run(null));
allPassed &= RunSuite("C140-ScrollView", GuideXosScrollViewC140Tests.TotalCaseCount,
    () => GuideXosScrollViewC140Tests.Run(null));
allPassed &= RunSuite("C141-VerticalStack", GuideXosVerticalStackC141Tests.TotalCaseCount,
    () => GuideXosVerticalStackC141Tests.Run(null));
allPassed &= RunSuite("C142-MarginsAlignment", GuideXosVerticalStackC142Tests.TotalCaseCount,
    () => GuideXosVerticalStackC142Tests.Run(null));
allPassed &= RunSuite("C145-Dialogs", 49,
    () => GuideXosDialogC145Tests.Run(null,
        new GuideXosDialog(112, 92, 336, 160, "C169 dialogs")));
allPassed &= RunSuite("C146-Settings", 11,
    () => GuideXosSettingsCenterC146Tests.Run(null));
allPassed &= RunSuite("C146-StoreFormat", 23,
    () => ManagedSettingsStoreC146Tests.Run(null));
allPassed &= RunSuite("C169-ListBoxRendering", GuideXosListBoxC169Tests.CaseCount,
    () => GuideXosListBoxC169Tests.Run(null));
bool stressPassed = RunSuite("C169-Stress", 4,
    () => GuideXosManagedUiStressC169Tests.Run(null));
allPassed &= stressPassed;
Console.WriteLine($"C169-STRESS popupCycles={GuideXosManagedUiStressC169Tests.PopupCycleCount} scrollViewMovements={GuideXosManagedUiStressC169Tests.ScrollMovementCount} c168Replays={GuideXosManagedUiStressC169Tests.SettingsLifecycleCycleCount} c168ReplayPageCycles=25000 c168ReplaySelectionChanges=25000 c168ReplayApplyResetCycles=10000 c168ReplayPopupCycles=25000 settingsCloseReopenCycles={GuideXosManagedUiStressC169Tests.SettingsLifecycleCycleCount} result={(stressPassed ? "PASS" : "FAIL")}");

allPassed &= RunSuite("C170-ControlRendering",
    GuideXosControlRenderingC170Tests.CaseCount,
    () => GuideXosControlRenderingC170Tests.Run(null));
bool c170StressPassed = RunSuite("C170-Stress", 7,
    () => GuideXosControlRenderingC170Tests.RunStress(null));
allPassed &= c170StressPassed;
Console.WriteLine($"C170-STRESS buttonFocusChanges={GuideXosControlRenderingC170Tests.ButtonFocusChangeCount} buttonActivations={GuideXosControlRenderingC170Tests.ButtonActivationCount} comboFocusChanges={GuideXosControlRenderingC170Tests.ComboFocusChangeCount} dropdownCycles={GuideXosControlRenderingC170Tests.DropDownCycleCount} keyboardSelectionChanges={GuideXosControlRenderingC170Tests.KeyboardSelectionChangeCount} pointerSelectionChanges={GuideXosControlRenderingC170Tests.PointerSelectionChangeCount} settingsCloseReopenCycles={GuideXosControlRenderingC170Tests.SettingsCloseReopenCycleCount} result={(c170StressPassed ? "PASS" : "FAIL")}");

allPassed &= RunSuite("C171-WindowLayout",
    GuideXosManagedWindowLayoutC171Tests.CaseCount,
    () => GuideXosManagedWindowLayoutC171Tests.Run(null));
Console.WriteLine($"C171-WINDOW-LAYOUT-DETAIL firstFailedCase={GuideXosManagedWindowLayoutC171Tests.FirstFailedCase}");
Console.WriteLine($"C171-MINIMUM-APPS-DETAIL {GuideXosManagedWindowLayoutC171Tests.MinimumAppsDetail}");
bool c171StressPassed = RunSuite("C171-Stress",
    GuideXosManagedWindowLayoutC171Tests.StressCaseCount,
    () => GuideXosManagedWindowLayoutC171Tests.RunStress(null));
allPassed &= c171StressPassed;
Console.WriteLine($"C171-STRESS layout={GuideXosManagedWindowLayoutC171Tests.StressLayoutCount} geometry={GuideXosManagedWindowLayoutC171Tests.StressGeometryChanges} focus={GuideXosManagedWindowLayoutC171Tests.StressFocusTransitions} scroll={GuideXosManagedWindowLayoutC171Tests.StressScrollMovements} combo={GuideXosManagedWindowLayoutC171Tests.StressComboCycles} lifecycle={GuideXosManagedWindowLayoutC171Tests.StressLifecycleIterations} allocationBytes={GuideXosManagedWindowLayoutC171Tests.StressAllocationBytes} drift={GuideXosManagedWindowLayoutC171Tests.StressDriftCount} result={(c171StressPassed ? "PASS" : "FAIL")}");
Console.WriteLine($"C171-STRESS-DETAIL generalPageAllocationBytes={GuideXosManagedWindowLayoutC171Tests.StressGeneralPageAllocationBytes} defaultAppsAttach={GuideXosManagedWindowLayoutC171Tests.StressAttachDiagnostics}");
Console.WriteLine($"C171-STRESS-RESULT-DETAIL {GuideXosManagedWindowLayoutC171Tests.StressFailureDetail}");

allPassed &= RunSuite("C172-WindowResize",
    GuideXosManagedWindowResizeC172Tests.CaseCount,
    () => GuideXosManagedWindowResizeC172Tests.Run(null));
bool c172StressPassed = RunSuite("C172-Stress",
    GuideXosManagedWindowResizeC172Tests.StressCaseCount,
    () => GuideXosManagedWindowResizeC172Tests.RunStress(null));
allPassed &= c172StressPassed;
Console.WriteLine($"C172-STRESS geometry={GuideXosManagedWindowResizeC172Tests.StressGeometryUpdates} transitions={GuideXosManagedWindowResizeC172Tests.StressAlternatingTransitions} focus={GuideXosManagedWindowResizeC172Tests.StressFocusTransitions} combo={GuideXosManagedWindowResizeC172Tests.StressComboCycles} scroll={GuideXosManagedWindowResizeC172Tests.StressScrollMovements} lifecycle={GuideXosManagedWindowResizeC172Tests.StressLifecycleIterations} allocationBytes={GuideXosManagedWindowResizeC172Tests.StressAllocationBytes} drift={GuideXosManagedWindowResizeC172Tests.StressDriftCount} result={(c172StressPassed ? "PASS" : "FAIL")}");
Console.WriteLine($"C172-STRESS-DETAIL firstFailedCase={GuideXosManagedWindowResizeC172Tests.FirstFailedCase} failure={GuideXosManagedWindowResizeC172Tests.StressFailureDetail}");

allPassed &= RunSuite("C173-ExplorerResize",
    GuideXosManagedWindowResizeC173Tests.CaseCount,
    () => GuideXosManagedWindowResizeC173Tests.Run(null));
Console.WriteLine($"C173-WINDOW-RESIZE-DETAIL firstFailedCase={GuideXosManagedWindowResizeC173Tests.FirstFailedCase}");
bool c173StressPassed = RunSuite("C173-ExplorerStress",
    GuideXosManagedWindowResizeC173Tests.StressCaseCount,
    () => GuideXosManagedWindowResizeC173Tests.RunStress(null));
allPassed &= c173StressPassed;
Console.WriteLine($"C173-STRESS layoutUpdates={GuideXosManagedWindowResizeC173Tests.StressLayoutUpdates} alternatingResizes={GuideXosManagedWindowResizeC173Tests.StressAlternatingResizes} selections={GuideXosManagedWindowResizeC173Tests.StressSelectionTransitions} scrolls={GuideXosManagedWindowResizeC173Tests.StressScrollOperations} keyboard={GuideXosManagedWindowResizeC173Tests.StressKeyboardNavigations} navigationResize={GuideXosManagedWindowResizeC173Tests.StressNavigationResizeOperations} lifecycles={GuideXosManagedWindowResizeC173Tests.StressLifecycleCycles} allocationBytes={GuideXosManagedWindowResizeC173Tests.StressLayoutAllocationBytes} drift={GuideXosManagedWindowResizeC173Tests.StressDriftCount} result={(c173StressPassed ? "PASS" : "FAIL")}");
Console.WriteLine($"C173-STRESS-DETAIL failure={GuideXosManagedWindowResizeC173Tests.StressFailureDetail}");

allPassed &= RunSuite("C174-SharedResize",
    GuideXosManagedWindowResizeC174Tests.CaseCount,
    () => GuideXosManagedWindowResizeC174Tests.Run(null));
Console.WriteLine($"C174-WINDOW-RESIZE-DETAIL firstFailedCase={GuideXosManagedWindowResizeC174Tests.FirstFailedCase}");
bool c174StressPassed = RunSuite("C174-Stress",
    GuideXosManagedWindowResizeC174Tests.StressCaseCount,
    () => GuideXosManagedWindowResizeC174Tests.RunStress(null));
allPassed &= c174StressPassed;
Console.WriteLine($"C174-STRESS geometry={GuideXosManagedWindowResizeC174Tests.StressGeometryTransitions} alternating={GuideXosManagedWindowResizeC174Tests.StressAlternatingCycles} isolation={GuideXosManagedWindowResizeC174Tests.StressIsolationTransitions} rollback={GuideXosManagedWindowResizeC174Tests.StressRollbackCount} allocationBytes={GuideXosManagedWindowResizeC174Tests.StressAllocationBytes} drift={GuideXosManagedWindowResizeC174Tests.StressDriftCount} result={(c174StressPassed ? "PASS" : "FAIL")}");

allPassed &= RunSuite("C175-NotesResize",
    GuideXosManagedWindowResizeC175Tests.CaseCount,
    () => GuideXosManagedWindowResizeC175Tests.Run(null));
Console.WriteLine($"C175-NOTES-RESIZE-DETAIL firstFailedCase={GuideXosManagedWindowResizeC175Tests.FirstFailedCase}");
bool c175StressPassed = RunSuite("C175-NotesResizeStress",
    GuideXosManagedWindowResizeC175Tests.StressCaseCount,
    () => GuideXosManagedWindowResizeC175Tests.RunStress(null));
allPassed &= c175StressPassed;
Console.WriteLine($"C175-STRESS layout={GuideXosManagedWindowResizeC175Tests.StressLayoutUpdates} alternating={GuideXosManagedWindowResizeC175Tests.StressAlternatingResizes} failed={GuideXosManagedWindowResizeC175Tests.StressFailedGeometryRequests} rollback={GuideXosManagedWindowResizeC175Tests.StressRollbackCount} lifecycles={GuideXosManagedWindowResizeC175Tests.StressLifecycleCount} allocationBytes={GuideXosManagedWindowResizeC175Tests.StressAllocationBytes} drift={GuideXosManagedWindowResizeC175Tests.StressDriftCount} result={(c175StressPassed ? "PASS" : "FAIL")}");

allPassed &= RunSuite("C176-NotesNavigation",
    GuideXosTextAreaC176Tests.CaseCount,
    () => GuideXosTextAreaC176Tests.Run(null));
Console.WriteLine($"C176-NOTES-NAVIGATION-DETAIL firstFailedCase={GuideXosTextAreaC176Tests.FirstFailedCase}");
bool c176StressPassed = RunSuite("C176-NotesNavigationStress",
    GuideXosTextAreaC176Tests.StressCaseCount,
    () => GuideXosTextAreaC176Tests.RunStress(null));
allPassed &= c176StressPassed;
Console.WriteLine($"C176-STRESS navigation={GuideXosTextAreaC176Tests.NavigationOperationCount} alternating={GuideXosTextAreaC176Tests.AlternatingResizeCount} navigationResize={GuideXosTextAreaC176Tests.NavigationResizeCount} rejected={GuideXosTextAreaC176Tests.RejectedGeometryCount} lifecycles={GuideXosTextAreaC176Tests.LifecycleCount} allocationBytes={GuideXosTextAreaC176Tests.ResizeAllocationBytes} drift={GuideXosTextAreaC176Tests.DriftCount} result={(c176StressPassed ? "PASS" : "FAIL")}");

allPassed &= RunSuite("C177-NotesNewline",
    GuideXosTextAreaC177Tests.CaseCount,
    () => GuideXosTextAreaC177Tests.Run(null));
Console.WriteLine($"C177-NOTES-NEWLINE-DETAIL firstFailedCase={GuideXosTextAreaC177Tests.FirstFailedCase}");
bool c177StressPassed = RunSuite("C177-NotesNewlineStress",
    GuideXosTextAreaC177Tests.StressCaseCount,
    () => GuideXosTextAreaC177Tests.RunStress(null));
allPassed &= c177StressPassed;
Console.WriteLine($"C177-STRESS operations={GuideXosTextAreaC177Tests.EditingNavigationOperationCount} mixedInsertions={GuideXosTextAreaC177Tests.MixedInsertionCount} mixedRemovals={GuideXosTextAreaC177Tests.MixedRemovalCount} capacityRejected={GuideXosTextAreaC177Tests.CapacityRejectionCount} alternatingResizes={GuideXosTextAreaC177Tests.AlternatingResizeCount} rejectedGeometry={GuideXosTextAreaC177Tests.RejectedGeometryCount} lifecycles={GuideXosTextAreaC177Tests.LifecycleCount} roundTrips={GuideXosTextAreaC177Tests.RoundTripCount} resizeAllocationBytes={GuideXosTextAreaC177Tests.ResizeAllocationBytes} geometryDrift={GuideXosTextAreaC177Tests.GeometryDriftCount} result={(c177StressPassed ? "PASS" : "FAIL")}");

Console.WriteLine($"MANAGED-UI-HOST-GATE suites={passedSuites + failedSuites} passed={passedSuites} failed={failedSuites} cases={totalCases} result={(allPassed && failedSuites == 0 ? "PASS" : "FAIL")}");
return allPassed && failedSuites == 0 ? 0 : 1;
