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

Console.WriteLine($"MANAGED-UI-HOST-GATE suites={passedSuites + failedSuites} passed={passedSuites} failed={failedSuites} cases={totalCases} result={(allPassed && failedSuites == 0 ? "PASS" : "FAIL")}");
return allPassed && failedSuites == 0 ? 0 : 1;
