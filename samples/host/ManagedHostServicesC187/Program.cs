using HostLogProof;

bool c185Passed = GuideXosManagedHostServicesC185Tests.Run();
Console.WriteLine($"C185-HOST-SERVICES cases={GuideXosManagedHostServicesC185Tests.FocusedCaseCount} firstFailedCase={GuideXosManagedHostServicesC185Tests.FirstFailedCase} result={(c185Passed ? "PASS" : "FAIL")}");
bool c186Passed = GuideXosManagedHostServicesC185Tests.RunC186();
Console.WriteLine($"C186-HOST-SERVICES cases={GuideXosManagedHostServicesC185Tests.FocusedCaseCountC186 + GuideXosManagedHostServicesC185Tests.StressCheckCountC186} firstFailedCase={GuideXosManagedHostServicesC185Tests.C186FirstFailedCase} result={(c186Passed ? "PASS" : "FAIL")}");
bool c187Passed = GuideXosManagedHostServicesC185Tests.RunC187();
Console.WriteLine($"C187-GUEST-CANDIDATE cases={GuideXosManagedHostServicesC185Tests.FocusedCaseCountC187} firstFailedCase={GuideXosManagedHostServicesC185Tests.C187FirstFailedCase} resizeStressAllocatedBytes={GuideXosManagedHostServicesC185Tests.C187ResizeStressAllocatedBytes} detail={GuideXosManagedHostServicesC185Tests.C187FailureDetail} result={(c187Passed ? "PASS" : "FAIL")}");
return c185Passed && c186Passed && c187Passed ? 0 : 1;
