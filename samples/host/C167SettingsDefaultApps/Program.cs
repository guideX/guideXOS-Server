using HostLogProof;
using HostLogProof.Applications;

ManagedSettingsCenter settings = new();
bool passed = GuideXosDefaultAppsC167Tests.Run(null, settings);
Console.WriteLine(passed
    ? "C167 host UI/state cases=20 stress=60 result=PASS"
    : $"C167 host UI/state cases={GuideXosDefaultAppsC167Tests.CasesExecuted} firstFailure={GuideXosDefaultAppsC167Tests.FirstFailedCase} detail={GuideXosDefaultAppsC167Tests.FailureDetail} result=FAIL");
return passed ? 0 : 1;
