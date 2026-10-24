using HostLogProof;
using HostLogProof.Applications;

internal static class NotesProofInvocationC188R7Regression
{
    public static bool Run()
    {
        int cases = 0;
        bool passed = true;
        void Check(string name, ReadOnlySpan<byte> context,
            bool c137SetupContext, bool c138SetupContext, bool alreadyRun,
            bool expected)
        {
            bool actual = GuideXosNotesProofTestInvocation
                .ShouldRunC137MouseWheelTests(context, c137SetupContext,
                    c138SetupContext, alreadyRun);
            ++cases;
            if (actual != expected)
            {
                passed = false;
                Console.WriteLine($"C188R7-C137-AUTH case={name} " +
                    $"expected={expected} actual={actual} result=FAIL");
            }
        }

        Check("c137-control", "c137"u8, true, false, false, true);
        Check("c137-native", "c137-native"u8, true, false, false, true);
        Check("c137-api", "c137-api"u8, true, false, false, true);
        Check("c137-host-tests", "c137-host-tests"u8, true, false,
            false, true);
        Check("c137-relaunch", "c137-relaunch"u8, true, false,
            false, true);
        Check("ordinary-notes", ReadOnlySpan<byte>.Empty, false, false,
            false, false);
        Check("ordinary-with-inherited-c137-setup",
            ReadOnlySpan<byte>.Empty, true, false, false, false);
        Check("c147-native-notes-with-c151-c152-merge",
            "c147-native-notes"u8, true, false, false, false);
        Check("c151-setup-only", "c151-notes"u8, true, false,
            false, false);
        Check("c152-setup-only", "c152-notes"u8, true, false,
            false, false);
        Check("c137-suppressed-by-c138-context", "c137"u8, true,
            true, false, false);
        Check("explicit-context-without-c137-setup", "c137"u8, false,
            false, false, false);
        Check("already-run", "c137"u8, true, false, true, false);
        Check("near-match-does-not-authorize", "c137-host-test"u8,
            true, false, false, false);

        bool c133NormalSkipped =
            !GuideXosNotesProofTestInvocation.ShouldRunC133ComboTests(
                "c147-native-notes"u8, false) &&
            !GuideXosNotesProofTestInvocation.ShouldRunC133HostTests(
                "c147-native-notes"u8, false);
        bool c133Explicit =
            GuideXosNotesProofTestInvocation.ShouldRunC133ComboTests(
                "c133-api"u8, false) &&
            GuideXosNotesProofTestInvocation.ShouldRunC133ComboTests(
                "c133-combo-tests"u8, false) &&
            GuideXosNotesProofTestInvocation.ShouldRunC133HostTests(
                "c133-host"u8, false) &&
            GuideXosNotesProofTestInvocation.ShouldRunC133HostTests(
                "c133-combo-host-tests"u8, false);
        bool c134Explicit =
            GuideXosNotesProofTestInvocation.ShouldRunC134HostTests(
                "c134-host-tests"u8, false) &&
            !GuideXosNotesProofTestInvocation.ShouldRunC134HostTests(
                "c147-native-notes"u8, false);
        ++cases;
        passed &= c133NormalSkipped && c133Explicit && c134Explicit;
        if (!c133NormalSkipped || !c133Explicit || !c134Explicit)
        {
            Console.WriteLine("C188R7-LEGACY-AUTH C133/C134 result=FAIL");
        }

        bool c137CasesPassed = GuideXosMouseWheelC137Tests.Run(null);
        passed &= c137CasesPassed;
        Console.WriteLine(
            $"C188R7-NOTES-PROOF-AUTH cases={cases} c137Cases=46 " +
            $"legacyC133C134={(c133NormalSkipped && c133Explicit && c134Explicit ? "PASS" : "FAIL")} " +
            $"c137Suite={(c137CasesPassed ? "PASS" : "FAIL")} " +
            $"result={(passed && cases == 15 ? "PASS" : "FAIL")}");
        return passed && cases == 15;
    }
}
