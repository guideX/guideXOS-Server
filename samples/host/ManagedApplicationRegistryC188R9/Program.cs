using HostLogProof;

bool passed = GuideXosApplicationRegistryC188R9Tests.Run();
Console.WriteLine($"C188R9-REGISTRY cases={GuideXosApplicationRegistryC188R9Tests.CaseCount} firstFailedCase={GuideXosApplicationRegistryC188R9Tests.FirstFailedCase} result={(passed ? "PASS" : "FAIL")}");
return passed ? 0 : 1;
