using HostLogProof;
using HostLogProof.Applications;

ManagedSettingsCenter settings = new();
bool c167Passed = GuideXosDefaultAppsC167Tests.Run(null, settings);
Console.WriteLine(c167Passed
    ? "C167 host UI/state cases=20 stress=60 result=PASS"
    : $"C167 host UI/state cases={GuideXosDefaultAppsC167Tests.CasesExecuted} firstFailure={GuideXosDefaultAppsC167Tests.FirstFailedCase} detail={GuideXosDefaultAppsC167Tests.FailureDetail} result=FAIL");
bool c168Passed = GuideXosDefaultAppsC168Tests.Run(null, settings);
Console.WriteLine(c168Passed
    ? "C168 host UI/layout cases=31 pageCycles=250 selectionChanges=250 applyResetCycles=100 popupCycles=250 result=PASS"
    : $"C168 host UI/layout cases={GuideXosDefaultAppsC168Tests.CasesExecuted} firstFailure={GuideXosDefaultAppsC168Tests.FirstFailedCase} detail={GuideXosDefaultAppsC168Tests.FailureDetail} result=FAIL");
return c167Passed && c168Passed ? 0 : 1;
