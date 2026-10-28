using HostLogProof;

bool r10Passed = GuideXosApplicationRegistryC188R10Tests.Run();
bool r11Passed = GuideXosApplicationRegistryC188R11Tests.Run();
bool r12r2Passed = GuideXosApplicationRegistryC188R12R2Tests.Run();
Console.WriteLine($"C188R10-STATE-REENTRY cases={GuideXosApplicationRegistryC188R10Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R10Tests.FirstFailedCase} result={(r10Passed ? "PASS" : "FAIL")}");
Console.WriteLine($"C188R11-STATE-PROVENANCE cases={GuideXosApplicationRegistryC188R11Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R11Tests.FirstFailedCase} result={(r11Passed ? "PASS" : "FAIL")}");
Console.WriteLine($"C188R12R2-LAUNCH-BOUNDARY cases={GuideXosApplicationRegistryC188R12R2Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R12R2Tests.FirstFailedCase} result={(r12r2Passed ? "PASS" : "FAIL")}");
if (!r12r2Passed) Console.WriteLine($"C188R12R2-DEBUG {GuideXosApplicationRegistryC188R12R2Tests.FailureDetail}");
return r10Passed && r11Passed && r12r2Passed ? 0 : 1;
