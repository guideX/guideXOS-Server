#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedNotesNewDocumentC157Tests
{
    private static int s_cases;

    public static bool Run(GuideXosHost host)
    {
        s_cases = 0;
        GuideXosClipboard clipboard = GuideXosClipboard.Shared;
        byte[] priorClipboard = new byte[GuideXosClipboard.Capacity];
        clipboard.TryCopyText(priorClipboard, out int priorLength);

        bool initialAndNamedReset = InitialAndNamedReset();
        bool clipboardHistory = ClipboardAndHistory(clipboard);
        bool savePointDirty = SavePointDirtyState();
        bool dirtyDecisionState = DirtyDecisionState();
        bool settingsReturn = CleanUntitledSettingsReturn();
        bool noShortcutLeak = CtrlNDoesNotInsertN();
        bool stress = NewStress(clipboard);

        clipboard.TrySetText(priorClipboard.AsSpan(0, priorLength));
        bool passed = initialAndNamedReset && clipboardHistory &&
            savePointDirty && dirtyDecisionState && settingsReturn &&
            noShortcutLeak && stress;
        if (!passed)
        {
            host?.TryLog("C157-NEW-TESTS result=FAIL"u8);
        }
        else
        {
            Span<byte> line = stackalloc byte[48];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C157-NEW-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)s_cases);
            GuideXosText.Append(line, ref position, " result=PASS"u8);
            host?.TryLog(line[..position]);
            host?.TryLog("C157-NEW-DETAIL document-state=PASS dirty-decisions=PASS history=PASS clipboard=PASS session=PASS shortcut=PASS result=PASS"u8);
            host?.TryLog("C157-NEW-STRESS cycles=25 same-instance=PASS history-reset=PASS clipboard-stable=PASS result=PASS"u8);
        }
        return passed;
    }

    private static bool InitialAndNamedReset()
    {
        GuideXosTextArea area = NewArea();
        GuideXosNotesDocumentState state = new();
        bool initial = Check(state.InitializeUntitled(area) &&
            !state.HasCurrentPath && state.CurrentPath.Length == 0 &&
            area.Text.Length == 0 && !state.Dirty && !area.CanUndo &&
            !area.CanRedo && area.CaretIndex == 0 && area.AnchorIndex == 0 &&
            area.FirstVisibleLine == 0 && state.HasSavedRevision &&
            state.SavedRevision == area.CurrentRevision);

        bool named = Check(state.TryHydrate(area,
            "/system/apps/C157/alpha.txt",
            "line0\nline1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\nline9"u8) &&
            state.HasCurrentPath && !state.Dirty);
        area.Focus();
        Select(area, 0, 2);
        bool scrolled = Check(area.SetFirstVisibleLine(5) &&
            area.FirstVisibleLine == 5 && area.HasSelection);
        bool edit = Check(area.HandleCharacter('!') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty && area.CanUndo);
        area.Undo();
        area.Redo();
        bool historyBefore = Check(area.CanUndo && area.CanRedo == false &&
            state.Dirty);
        area.SetCaretToStart();
        area.HandleKey(GuideXosTextInputKey.Right, true);
        bool viewBefore = Check(area.HasSelection && area.AnchorIndex == 0 &&
            area.CaretIndex == 1 && area.SetFirstVisibleLine(5) &&
            area.FirstVisibleLine == 5);
        bool reset = Check(state.InitializeUntitled(area) &&
            !state.HasCurrentPath && state.CurrentPath.Length == 0 &&
            area.Text.Length == 0 && !state.Dirty && !area.CanUndo &&
            !area.CanRedo && area.CaretIndex == 0 && area.AnchorIndex == 0 &&
            !area.HasSelection &&
            area.FirstVisibleLine == 0 && state.SavedRevisionReachable);
        bool cleanAgain = Check(state.InitializeUntitled(area) &&
            !state.Dirty && !area.CanUndo && !area.CanRedo);
        return initial && named && scrolled && edit && historyBefore &&
            viewBefore && reset && cleanAgain;
    }

    private static bool ClipboardAndHistory(GuideXosClipboard clipboard)
    {
        clipboard.Clear();
        GuideXosTextArea area = NewArea();
        GuideXosNotesDocumentState state = new();
        bool emptyStart = state.InitializeUntitled(area) &&
            !clipboard.HasText;
        bool emptyClipboardNew = state.InitializeUntitled(area) &&
            !clipboard.HasText && !state.Dirty && !area.CanUndo &&
            !area.CanRedo;
        bool loaded = state.TryHydrate(area, "/system/apps/C157/copy.txt",
            "copy source"u8);
        area.Focus();
        Select(area, 0, 4);
        bool copied = clipboard.TryCopySelectionFrom(area) &&
            clipboard.TextSpan.SequenceEqual("copy"u8);
        bool newDocument = state.InitializeUntitled(area) &&
            !state.HasCurrentPath && area.Text.Length == 0 &&
            !state.Dirty && !area.CanUndo && !area.CanRedo &&
            clipboard.TextSpan.SequenceEqual("copy"u8);
        area.Focus();
        bool paste = area.PasteText(clipboard.TextSpan) ==
            GuideXosTextAreaEditResult.Changed && area.Text == "copy" &&
            state.Dirty && area.CanUndo &&
            clipboard.TextSpan.SequenceEqual("copy"u8);
        bool undoBaseline = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.Text.Length == 0 && !state.Dirty && !area.CanUndo &&
            area.CanRedo && state.HasSavedRevision &&
            state.SavedRevision == area.CurrentRevision;
        bool redoDirty = area.Redo() == GuideXosTextAreaEditResult.Changed &&
            state.Dirty && area.Text == "copy" && clipboard.HasText;
        bool resetAgain = state.InitializeUntitled(area) &&
            !state.Dirty && !area.CanUndo && !area.CanRedo &&
            clipboard.TextSpan.SequenceEqual("copy"u8);
        GuideXosTextArea cutArea = NewArea();
        GuideXosNotesDocumentState cutState = new();
        bool cutLoaded = cutState.TryHydrate(cutArea,
            "/system/apps/C157/cut.txt", "cut source"u8);
        cutArea.Focus();
        Select(cutArea, 0, 3);
        bool cut = cutArea.CutSelection(clipboard) ==
                GuideXosTextAreaEditResult.Changed &&
            cutArea.Text == " source" && cutState.Dirty &&
            clipboard.TextSpan.SequenceEqual("cut"u8);
        bool cutNew = cutState.InitializeUntitled(cutArea) &&
            !cutState.HasCurrentPath && cutArea.Text.Length == 0 &&
            !cutState.Dirty && !cutArea.CanUndo && !cutArea.CanRedo &&
            clipboard.TextSpan.SequenceEqual("cut"u8);
        return Check(emptyStart) && Check(emptyClipboardNew) &&
            Check(loaded) && Check(copied) && Check(newDocument) &&
            Check(paste) && Check(undoBaseline) && Check(redoDirty) &&
            Check(resetAgain) && Check(cutLoaded) && Check(cut) &&
            Check(cutNew);
    }

    private static bool SavePointDirtyState()
    {
        GuideXosTextArea area = NewArea();
        GuideXosNotesDocumentState state = new();
        bool start = state.TryHydrate(area, "/system/apps/C157/save.txt",
            "A"u8);
        area.Focus();
        bool edit = area.HandleCharacter('B') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty;
        bool saveAsKeepsHistory = state.MarkSaveSucceeded(
            "/system/apps/C157/new.txt") && !state.Dirty && area.CanUndo &&
            state.CurrentPath == "/system/apps/C157/new.txt";
        bool undoDirty = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.Text == "A" && state.Dirty &&
            state.CurrentPath == "/system/apps/C157/new.txt";
        bool redoClean = area.Redo() == GuideXosTextAreaEditResult.Changed &&
            area.Text == "AB" && !state.Dirty;

        GuideXosTextArea savePointArea = NewArea();
        GuideXosNotesDocumentState savePointState = new();
        bool savePoint = savePointState.TryHydrate(savePointArea,
            "/system/apps/C157/saved.txt", "A"u8);
        savePointArea.Focus();
        savePointArea.HandleCharacter('B');
        bool undoToSavePoint = savePointArea.Undo() ==
            GuideXosTextAreaEditResult.Changed && !savePointState.Dirty &&
            savePointArea.CanRedo;
        bool noPromptAfterUndo = !savePointState.Dirty &&
            savePointState.InitializeUntitled(savePointArea) &&
            !savePointState.Dirty && !savePointArea.CanUndo &&
            !savePointArea.CanRedo;

        GuideXosTextArea redoArea = NewArea();
        GuideXosNotesDocumentState redoState = new();
        bool redoStart = redoState.TryHydrate(redoArea,
            "/system/apps/C157/redo.txt", "A"u8);
        redoArea.Focus();
        redoArea.HandleCharacter('B');
        bool redoAwayDirty = redoArea.Undo() == GuideXosTextAreaEditResult.Changed &&
            !redoState.Dirty && redoArea.Redo() ==
                GuideXosTextAreaEditResult.Changed && redoState.Dirty;
        return Check(start) && Check(edit) && Check(saveAsKeepsHistory) &&
            Check(undoDirty) && Check(redoClean) && Check(savePoint) &&
            Check(undoToSavePoint) && Check(noPromptAfterUndo) &&
            Check(redoStart) && Check(redoAwayDirty);
    }

    private static bool CleanUntitledSettingsReturn()
    {
        GuideXosTextArea area = NewArea();
        GuideXosNotesDocumentState state = new();
        GuideXosNotesReturnSessionC155 slot =
            GuideXosNotesReturnSessionC155.Shared;
        slot.Clear();
        bool blank = state.InitializeUntitled(area);
        bool armed = slot.TryCaptureResolved(state, area,
            GuideXosNotesSessionDecisionC155.Clean, out _);
        bool consumed = slot.TryConsume(true,
            out GuideXosNotesReturnSessionDataC155 session);
        bool blankSession = consumed && session.IsValid && !session.HasPath &&
            session.Path == null && session.CaretIndex == 0 &&
            session.AnchorIndex == 0 && session.FirstVisibleLine == 0;
        bool restored = blankSession && state.InitializeUntitled(area) &&
            !state.HasCurrentPath && !state.Dirty && area.Text.Length == 0 &&
            !area.CanUndo && !area.CanRedo && area.CaretIndex == 0 &&
            area.AnchorIndex == 0 && area.FirstVisibleLine == 0;
        slot.Clear();
        return Check(blank) && Check(armed) && Check(consumed) &&
            Check(blankSession) && Check(restored);
    }

    private static bool DirtyDecisionState()
    {
        GuideXosTextArea namedArea = NewArea();
        GuideXosNotesDocumentState named = new();
        bool namedStart = named.TryHydrate(namedArea,
            "/system/apps/C157/decision.txt", "A"u8);
        namedArea.Focus();
        namedArea.HandleCharacter('B');
        string namedText = namedArea.Text;
        GuideXosTextRevision namedRevision = namedArea.CurrentRevision;
        int namedHistory = namedArea.HistoryCount;
        bool namedCancel = named.Dirty && namedArea.Text == namedText &&
            named.CurrentPath == "/system/apps/C157/decision.txt" &&
            namedArea.CurrentRevision == namedRevision &&
            namedArea.HistoryCount == namedHistory;
        bool namedSaveThenNew = named.MarkSaveSucceeded(named.CurrentPath) &&
            !named.Dirty && named.InitializeUntitled(namedArea) &&
            !named.HasCurrentPath && namedArea.Text.Length == 0 &&
            !named.Dirty && !namedArea.CanUndo && !namedArea.CanRedo;

        GuideXosTextArea discardArea = NewArea();
        GuideXosNotesDocumentState discard = new();
        bool discardStart = discard.TryHydrate(discardArea,
            "/system/apps/C157/discard.txt", "disk"u8);
        discardArea.Focus();
        discardArea.HandleCharacter('!');
        bool dirtyDiscard = discard.Dirty && discard.InitializeUntitled(discardArea) &&
            !discard.HasCurrentPath && discardArea.Text.Length == 0 &&
            !discard.Dirty && !discardArea.CanUndo && !discardArea.CanRedo;

        GuideXosTextArea untitledArea = NewArea();
        GuideXosNotesDocumentState untitled = new();
        bool untitledStart = untitled.InitializeUntitled(untitledArea);
        untitledArea.Focus();
        untitledArea.HandleCharacter('U');
        string untitledText = untitledArea.Text;
        GuideXosTextRevision untitledRevision = untitledArea.CurrentRevision;
        int untitledHistory = untitledArea.HistoryCount;
        untitled.MarkSaveFailed();
        bool saveAsCancelOrFailure = untitled.Dirty &&
            !untitled.HasCurrentPath && untitledArea.Text == untitledText &&
            untitledArea.CurrentRevision == untitledRevision &&
            untitledArea.HistoryCount == untitledHistory;
        bool untitledSaveAsThenNew = untitled.MarkSaveSucceeded(
                "/system/apps/C157/untitled.txt") && !untitled.Dirty &&
            untitled.HasCurrentPath && untitledArea.CanUndo &&
            untitled.InitializeUntitled(untitledArea) &&
            !untitled.HasCurrentPath && untitledArea.Text.Length == 0 &&
            !untitled.Dirty && !untitledArea.CanUndo && !untitledArea.CanRedo;

        return Check(namedStart) && Check(namedCancel) &&
            Check(namedSaveThenNew) && Check(discardStart) &&
            Check(dirtyDiscard) && Check(untitledStart) &&
            Check(saveAsCancelOrFailure) && Check(untitledSaveAsThenNew);
    }

    private static bool CtrlNDoesNotInsertN()
    {
        GuideXosTextArea area = NewArea();
        area.Focus();
        GuideXosInputEvent controlN = GuideXosInputEvent.ForKeyDown(
            (GuideXosTextInputKey)'n', control: true);
        GuideXosInputEvent controlNCharacter =
            GuideXosInputEvent.ForKeyChar('n', control: true);
        bool routesNew = ManagedNotes.MapC156Shortcut(controlN) ==
            ManagedNotes.C157NewDocumentActionId;
        bool consumesCharacter = ManagedNotes.ShouldConsumeC156KeyChar(
            controlNCharacter);
        if (!consumesCharacter)
            area.HandleCharacter(controlNCharacter.Character);
        bool noLeak = area.Text.Length == 0 && !area.CanUndo;
        return Check(routesNew) && Check(consumesCharacter) && Check(noLeak);
    }

    private static bool NewStress(GuideXosClipboard clipboard)
    {
        GuideXosTextArea area = NewArea();
        GuideXosNotesDocumentState state = new();
        clipboard.TrySetText("keep"u8);
        bool valid = state.InitializeUntitled(area);
        for (int cycle = 0; cycle < 25 && valid; cycle++)
        {
            if (!state.HasCurrentPath && !state.Dirty)
                valid = state.TryHydrate(area,
                    "/system/apps/C157/stress.txt", "A"u8);
            area.Focus();
            if (cycle % 3 == 0)
            {
                valid = valid && state.InitializeUntitled(area);
            }
            else if (cycle % 3 == 1)
            {
                valid = valid && area.PasteText(clipboard.TextSpan) ==
                    GuideXosTextAreaEditResult.Changed && state.Dirty &&
                    area.Undo() == GuideXosTextAreaEditResult.Changed &&
                    !state.Dirty && state.InitializeUntitled(area);
            }
            else
            {
                valid = valid && area.HandleCharacter('X') ==
                    GuideXosTextAreaEditResult.Changed && state.Dirty &&
                    state.InitializeUntitled(area);
            }
            valid = valid && !state.HasCurrentPath && !state.Dirty &&
                area.Text.Length == 0 && !area.CanUndo && !area.CanRedo &&
                area.CaretIndex == 0 && area.AnchorIndex == 0 &&
                area.FirstVisibleLine == 0 &&
                clipboard.TextSpan.SequenceEqual("keep"u8);
        }
        return Check(valid);
    }

    private static GuideXosTextArea NewArea() => new(256, 32, 4, 32);

    private static void Select(GuideXosTextArea area, int start, int end)
    {
        area.SetCaretToStart();
        for (int index = 0; index < start; index++)
            area.HandleKey(GuideXosTextInputKey.Right);
        for (int index = start; index < end; index++)
            area.HandleKey(GuideXosTextInputKey.Right, true);
    }

    private static bool Check(bool condition)
    {
        ++s_cases;
        return condition;
    }
}
#endif
