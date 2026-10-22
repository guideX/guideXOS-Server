using HostLogProof;

bool passed = GuideXosManagedHostServicesC185Tests.RunC188();
Console.WriteLine($"C188-EXTERNAL-IDENTITY cases={GuideXosManagedHostServicesC185Tests.FocusedCaseCountC188} firstFailedCase={GuideXosManagedHostServicesC185Tests.C188FirstFailedCase} resizeStressAllocatedBytes={GuideXosManagedHostServicesC185Tests.C188ResizeStressAllocatedBytes} detail={GuideXosManagedHostServicesC185Tests.C188FailureDetail} identityStress=1000 validResizes=500 invalidGeometry=250 staleGeneration=250 lifecycles=100 result={(passed ? "PASS" : "FAIL")}");
return passed ? 0 : 1;
