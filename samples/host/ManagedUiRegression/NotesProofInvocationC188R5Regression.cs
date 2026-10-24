using HostLogProof;
using HostLogProof.Applications;

internal static class NotesProofInvocationC188R5Regression
{
    public static bool Run()
    {
        int contextCases = 0;
        bool passed = true;
        void Check(string name, ReadOnlySpan<byte> context, bool expected,
            bool alreadyRun = false)
        {
            bool actual = GuideXosNotesProofTestInvocation
                .ShouldRunC134HostTests(context, alreadyRun);
            ++contextCases;
            if (actual != expected)
            {
                passed = false;
                Console.WriteLine($"C188R5-PROOF-PREDICATE case={name} " +
                    $"expected={expected} actual={actual} result=FAIL");
            }
        }

        Check("ordinary-notes", ""u8, false);
        Check("c147-native-notes-with-c151-c152-merge", "c147-native-notes"u8,
            false);
        Check("c151-context", "c151-notes"u8, false);
        Check("c152-context", "c152-notes"u8, false);
        Check("c133-control-context", "c133"u8, false);
        Check("c133-notes-context", "c133-notes"u8, false);
        Check("c133-disabled-context", "c133-disabled"u8, false);
        Check("c133-api-test-context", "c133-api"u8, false);
        Check("c133-combo-test-context", "c133-combo-tests"u8, false);
        Check("c133-host-test-context", "c133-host"u8, true);
        Check("c133-combo-host-test-context", "c133-combo-host-tests"u8,
            true);
        Check("c134-control-context", "c134"u8, false);
        Check("c134-host-test-context", "c134-host-tests"u8, true);
        Check("c135-merged-proof-context", "c135-host-tests"u8, false);
        Check("c136-merged-proof-context", "c136-host-tests"u8, false);
        Check("c137-merged-proof-context", "c137-host-tests"u8, false);
        Check("c139-merged-proof-context", "c139-host-tests"u8, false);
        Check("c151-context-merging", "c151-notes"u8, false);
        Check("c152-context-merging", "c152-notes"u8, false);
        Check("empty-proof-context", ReadOnlySpan<byte>.Empty, false);
        Check("c133-host-test-already-run", "c133-host"u8, false, true);
        Check("c134-host-test-already-run", "c134-host-tests"u8, false,
            true);
        Check("near-match-does-not-authorize", "c133-host-test"u8, false);

        bool c152DocumentStatePassed = RunC152DocumentStateCases();
        passed &= c152DocumentStatePassed;
        bool c133HostTestsPassed = GuideXosComboBoxC133HostTests.Run(null);
        passed &= c133HostTestsPassed;
        bool c134HostTestsPassed = GuideXosComboBoxC134HostTests.Run(null);
        passed &= c134HostTestsPassed;

        Console.WriteLine(
            $"C188R5-NOTES-PROOF-INVOCATION contextCases={contextCases} " +
            $"c133HostCases=24 c133HostResult={(c133HostTestsPassed ? "PASS" : "FAIL")} " +
            $"c134HostCases=30 c134HostResult={(c134HostTestsPassed ? "PASS" : "FAIL")} " +
            $"c152DocumentStateResult={(c152DocumentStatePassed ? "PASS" : "FAIL")} " +
            $"result={(passed ? "PASS" : "FAIL")}");
        return passed;
    }

    private static bool RunC152DocumentStateCases()
    {
        int cases = 0;
        bool passed = true;
        void Check(bool condition)
        {
            ++cases;
            passed &= condition;
        }

        const string savePath = "/system/apps/C188R5/first.txt";
        const string saveAsPath = "/system/apps/C188R5/second.txt";
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();
        bool initialized = state.InitializeUntitled(area);
        Check(initialized && !state.HasCurrentPath && !state.Dirty &&
            area.HistoryCount == 1);

        area.Focus();
        bool firstEdit = area.HandleCharacter('A') ==
            GuideXosTextAreaEditResult.Changed;
        Check(firstEdit && state.Dirty && !state.HasCurrentPath);
        int historyBeforeSave = area.HistoryCount;
        Check(state.MarkSaveSucceeded(savePath) && !state.Dirty &&
            state.CurrentPath == savePath &&
            area.HistoryCount == historyBeforeSave);

        bool secondEdit = area.HandleCharacter('B') ==
            GuideXosTextAreaEditResult.Changed;
        GuideXosTextRevision savedRevision = state.SavedRevision;
        state.MarkSaveFailed();
        Check(secondEdit && state.Dirty && state.CurrentPath == savePath &&
            state.SavedRevision == savedRevision);

        int historyBeforeSaveAs = area.HistoryCount;
        Check(state.MarkSaveSucceeded(saveAsPath) && !state.Dirty &&
            state.CurrentPath == saveAsPath &&
            area.HistoryCount == historyBeforeSaveAs);
        Check(!state.MarkSaveSucceeded("/invalid/path") &&
            state.CurrentPath == saveAsPath && !state.Dirty);

        Check(state.TryHydrate(area, saveAsPath, "hydrated"u8) &&
            area.Text == "hydrated" && !state.Dirty &&
            state.CurrentPath == saveAsPath && area.HistoryCount == 1);
        Check(!state.TryHydrate(area, "/invalid/path", "bad"u8) &&
            area.Text == "hydrated" && state.CurrentPath == saveAsPath &&
            !state.Dirty);

        Console.WriteLine(
            $"C188R5-C152-DOCUMENT-STATE cases={cases} " +
            $"result={(passed && cases == 8 ? "PASS" : "FAIL")}");
        return passed && cases == 8;
    }
}
