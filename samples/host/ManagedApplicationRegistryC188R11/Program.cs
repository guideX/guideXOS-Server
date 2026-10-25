using HostLogProof;

bool r10Passed = GuideXosApplicationRegistryC188R10Tests.Run();
bool r11Passed = GuideXosApplicationRegistryC188R11Tests.Run();
Console.WriteLine($"C188R10-STATE-REENTRY cases={GuideXosApplicationRegistryC188R10Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R10Tests.FirstFailedCase} result={(r10Passed ? "PASS" : "FAIL")}");
Console.WriteLine($"C188R11-STATE-PROVENANCE cases={GuideXosApplicationRegistryC188R11Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R11Tests.FirstFailedCase} result={(r11Passed ? "PASS" : "FAIL")}");
return r10Passed && r11Passed ? 0 : 1;
