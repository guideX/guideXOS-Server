using System;

namespace HostLogProof;

public static class GuideXosNotesDocumentStateC153Tests
{
    public const int CaseCount = 17;

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool all = true;
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();

        bool openBaseline = state.TryHydrate(area,
                "/system/apps/C153/alpha.txt", "A"u8) && !state.Dirty &&
            !area.CanUndo && !area.CanRedo && state.SavedRevision ==
                area.CurrentRevision && state.SavedRevisionReachable;
        all &= Case(ref cases, openBaseline);

        bool editDirty = Type(area, "B") && state.Dirty && area.CanUndo &&
            area.Text == "AB";
        all &= Case(ref cases, editDirty);
        bool undoClean = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.Text == "A" && !state.Dirty && area.CanRedo;
        all &= Case(ref cases, undoClean);
        bool redoDirty = area.Redo() == GuideXosTextAreaEditResult.Changed &&
            area.Text == "AB" && state.Dirty;
        all &= Case(ref cases, redoDirty);

        bool saveCurrentClean = Type(area, "C") && state.Dirty;
        int historyBeforeSave = area.HistoryCount;
        saveCurrentClean &= state.MarkSaveSucceeded(
                "/system/apps/C153/alpha.txt") && !state.Dirty &&
            area.HistoryCount == historyBeforeSave &&
            state.SavedRevision == area.CurrentRevision;
        all &= Case(ref cases, saveCurrentClean);

        bool undoRedoSaved = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            state.Dirty && area.Text == "AB" && area.Redo() ==
                GuideXosTextAreaEditResult.Changed && !state.Dirty &&
            area.Text == "ABC";
        all &= Case(ref cases, undoRedoSaved);

        area.Undo();
        bool branchKillsRedo = Type(area, "D") && !area.CanRedo && state.Dirty &&
            area.Text == "ABD";
        all &= Case(ref cases, branchKillsRedo);

        bool saveAsClean = state.MarkSaveSucceeded(
                "/system/apps/C153/new.txt") && !state.Dirty &&
            state.CurrentPath == "/system/apps/C153/new.txt";
        bool saveAsPathIndependent = area.Undo() ==
                GuideXosTextAreaEditResult.Changed && state.Dirty &&
            state.CurrentPath == "/system/apps/C153/new.txt" &&
            area.Redo() == GuideXosTextAreaEditResult.Changed &&
            !state.Dirty && state.CurrentPath == "/system/apps/C153/new.txt";
        all &= Case(ref cases, saveAsClean && saveAsPathIndependent);

        area.Undo();
        GuideXosTextRevision failedSavePoint = state.SavedRevision;
        GuideXosTextRevision failedCurrent = area.CurrentRevision;
        int failedHistoryCount = area.HistoryCount;
        string failedPath = state.CurrentPath;
        state.MarkSaveFailed();
        bool failedSavePreserved = state.Dirty &&
            state.SavedRevision == failedSavePoint &&
            area.CurrentRevision == failedCurrent &&
            area.HistoryCount == failedHistoryCount &&
            state.CurrentPath == failedPath;
        all &= Case(ref cases, failedSavePreserved);

        bool cleanSaveFailure = area.Redo() ==
                GuideXosTextAreaEditResult.Changed && !state.Dirty;
        GuideXosTextRevision cleanSavedRevision = state.SavedRevision;
        state.MarkSaveFailed();
        cleanSaveFailure &= !state.Dirty &&
            state.SavedRevision == cleanSavedRevision;
        all &= Case(ref cases, cleanSaveFailure);

        GuideXosTextArea evictionArea = new(256, 32, 4, 32);
        GuideXosNotesDocumentState evictionState = new();
        bool evictionReady = evictionState.TryHydrate(evictionArea,
            "/system/apps/C153/eviction.txt", ReadOnlySpan<byte>.Empty);
        for (int index = 0; index < 20; index++)
        {
            evictionReady &= Type(evictionArea, "x");
        }
        bool savePointEvicted = evictionReady && evictionArea.HistoryCount == 16 &&
            !evictionState.SavedRevisionReachable && evictionState.Dirty;
        all &= Case(ref cases, savePointEvicted);

        while (evictionArea.CanUndo)
        {
            evictionArea.Undo();
        }
        bool evictionRemainsDirty = evictionArea.Text.Length == 5 &&
            !evictionArea.CanUndo && evictionState.Dirty &&
            !evictionState.SavedRevisionReachable;
        all &= Case(ref cases, evictionRemainsDirty);

        int retainedCount = evictionArea.HistoryCount;
        bool newSaveReachable = evictionState.MarkSaveSucceeded(
                "/system/apps/C153/eviction.txt") && !evictionState.Dirty &&
            evictionState.SavedRevisionReachable &&
            evictionArea.HistoryCount == retainedCount;
        all &= Case(ref cases, newSaveReachable);
        bool redoLeavesNewSave = evictionArea.Redo() ==
                GuideXosTextAreaEditResult.Changed && evictionState.Dirty &&
            evictionState.SavedRevisionReachable;
        all &= Case(ref cases, redoLeavesNewSave);

        bool replacementOpen = evictionState.TryHydrate(evictionArea,
                "/system/apps/C153/beta.txt", "BETA"u8) &&
            evictionArea.Text == "BETA" && evictionState.CurrentPath ==
                "/system/apps/C153/beta.txt" && !evictionState.Dirty &&
            evictionArea.HistoryCount == 1 && !evictionArea.CanUndo &&
            !evictionArea.CanRedo;
        all &= Case(ref cases, replacementOpen);

        GuideXosTextRevision openRevision = evictionArea.CurrentRevision;
        string openPath = evictionState.CurrentPath;
        bool failedOpenPreserved = !evictionState.TryHydrate(evictionArea,
                "/system/apps/C153/alpha.txt", new byte[257]) &&
            evictionArea.Text == "BETA" && evictionState.CurrentPath == openPath &&
            !evictionState.Dirty && evictionArea.HistoryCount == 1 &&
            evictionArea.CurrentRevision == openRevision;
        all &= Case(ref cases, failedOpenPreserved);

        bool branchRevisionIdentity = Type(evictionArea, "!") &&
            evictionState.Dirty;
        GuideXosTextRevision branchRevision = evictionArea.CurrentRevision;
        evictionArea.Undo();
        branchRevisionIdentity &= Type(evictionArea, "?") &&
            evictionArea.CurrentRevision != branchRevision &&
            !evictionArea.CanRedo && evictionState.Dirty;
        all &= Case(ref cases, branchRevisionIdentity);

        bool countCorrect = cases == CaseCount;
        host?.TryLog(all && countCorrect
            ? "C153-SAVE-POINT cases=17 Open=SaveAs=Undo=Redo=PASS eviction=dirty ABA=protected result=PASS"u8
            : "C153-SAVE-POINT result=FAIL"u8);
        return all && countCorrect;
    }

    private static bool Type(GuideXosTextArea area, string text)
    {
        area.Focus();
        for (int index = 0; index < text.Length; index++)
        {
            if (area.HandleCharacter(text[index]) !=
                GuideXosTextAreaEditResult.Changed)
                return false;
        }
        return true;
    }

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        return passed;
    }
}
