using HostLogProof;

bool passed = GuideXosApplicationRegistryC188R10Tests.Run();
Console.WriteLine($"C188R10-STATE-REENTRY cases={GuideXosApplicationRegistryC188R10Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R10Tests.FirstFailedCase} result={(passed ? "PASS" : "FAIL")}");
return passed ? 0 : 1;
