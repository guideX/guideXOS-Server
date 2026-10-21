using HostLogProof.Applications;

bool passed = ManagedStarterC182HostTests.Run();
Console.WriteLine($"C182-MANAGED-STARTER cases={ManagedStarterC182HostTests.CaseCount} firstFailedCase={ManagedStarterC182HostTests.FirstFailedCase} resizeTransactions={ManagedStarterC182HostTests.ResizeTransactionCount} resizeAllocationBytes={ManagedStarterC182HostTests.ResizeAllocationBytes} result={(passed ? "PASS" : "FAIL")}");
return passed ? 0 : 1;
