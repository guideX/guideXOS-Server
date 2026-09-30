using System;

namespace HostLogProof;

public static class GuideXosTextAreaUndoRedoC153Tests
{
    public const int CaseCount = 25;

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool all = true;

        GuideXosTextArea insert = New(string.Empty);
        int callbacks = 0;
        insert.ContentChanged += () => ++callbacks;
        bool inserted = insert.HandleCharacter('a') ==
                GuideXosTextAreaEditResult.Changed && insert.Text == "a" &&
            insert.HistoryCount == 2 && insert.CanUndo && callbacks == 1;
        all &= Case(ref cases, inserted);
        bool undoInsert = insert.Undo() == GuideXosTextAreaEditResult.Changed &&
            insert.Text.Length == 0 && insert.CaretIndex == 0 && callbacks == 2;
        all &= Case(ref cases, undoInsert);
        bool redoInsert = insert.Redo() == GuideXosTextAreaEditResult.Changed &&
            insert.Text == "a" && insert.CaretIndex == 1 && callbacks == 3;
        all &= Case(ref cases, redoInsert);

        GuideXosTextArea newline = New("ab");
        newline.HandleKey(GuideXosTextInputKey.Left);
        bool insertedNewline = newline.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && newline.Text == "a\nb" &&
            newline.Undo() == GuideXosTextAreaEditResult.Changed &&
            newline.Text == "ab" && newline.Redo() ==
                GuideXosTextAreaEditResult.Changed && newline.Text == "a\nb";
        all &= Case(ref cases, insertedNewline);

        GuideXosTextArea backspace = New("abc");
        bool backspaceRoundTrip = backspace.HandleKey(
                GuideXosTextInputKey.Backspace) == GuideXosTextAreaEditResult.Changed &&
            backspace.Text == "ab" && backspace.CaretIndex == 2 &&
            backspace.Undo() == GuideXosTextAreaEditResult.Changed &&
            backspace.Text == "abc" && backspace.CaretIndex == 3 &&
            backspace.Redo() == GuideXosTextAreaEditResult.Changed &&
            backspace.Text == "ab" && backspace.CaretIndex == 2;
        all &= Case(ref cases, backspaceRoundTrip);

        GuideXosTextArea delete = New("abc");
        delete.HandleKey(GuideXosTextInputKey.Left);
        bool deleteRoundTrip = delete.HandleKey(GuideXosTextInputKey.Delete) ==
                GuideXosTextAreaEditResult.Changed && delete.Text == "ab" &&
            delete.Undo() == GuideXosTextAreaEditResult.Changed &&
            delete.Text == "abc" && delete.CaretIndex == 2 &&
            delete.Redo() == GuideXosTextAreaEditResult.Changed &&
            delete.Text == "ab" && delete.CaretIndex == 2;
        all &= Case(ref cases, deleteRoundTrip);

        GuideXosTextArea selection = New("abcd");
        selection.HandleKey(GuideXosTextInputKey.Left);
        selection.HandleKey(GuideXosTextInputKey.Left, true);
        selection.HandleKey(GuideXosTextInputKey.Left, true);
        bool selectionReplace = selection.HasSelection &&
            selection.HandleCharacter('X') == GuideXosTextAreaEditResult.Changed &&
            selection.Text == "aXd" && selection.Undo() ==
                GuideXosTextAreaEditResult.Changed && selection.Text == "abcd" &&
            selection.HasSelection && selection.CaretIndex == 1 &&
            selection.AnchorIndex == 3;
        all &= Case(ref cases, selectionReplace);

        GuideXosTextArea navigation = New("abc\ndef");
        int navigationHistory = navigation.HistoryCount;
        GuideXosTextRevision navigationRevision = navigation.CurrentRevision;
        navigation.HandleKey(GuideXosTextInputKey.Left);
        navigation.HandleKey(GuideXosTextInputKey.Up);
        navigation.HandleKey(GuideXosTextInputKey.Home, true);
        all &= Case(ref cases, navigation.HistoryCount == navigationHistory &&
            navigation.CurrentRevision == navigationRevision &&
            !navigation.CanUndo);

        GuideXosTextArea wheel = New("1\n2\n3\n4\n5\n6\n7\n8");
        wheel.SetCaretToStart();
        int wheelHistory = wheel.HistoryCount;
        GuideXosTextRevision wheelRevision = wheel.CurrentRevision;
        GuideXosTextAreaEditResult wheelResult = wheel.HandleWheel(-8, 1);
        all &= Case(ref cases, wheelResult == GuideXosTextAreaEditResult.Scrolled &&
            wheel.FirstVisibleLine > 0 && wheel.HistoryCount == wheelHistory &&
            wheel.CurrentRevision == wheelRevision && !wheel.CanUndo);

        GuideXosTextArea full = New(new string('F', 256));
        int fullHistory = full.HistoryCount;
        int fullCallbacks = 0;
        full.ContentChanged += () => ++fullCallbacks;
        bool fullRejected = full.HandleCharacter('X') ==
                GuideXosTextAreaEditResult.Rejected && full.Length == 256 &&
            full.HistoryCount == fullHistory && !full.CanUndo &&
            fullCallbacks == 0;
        all &= Case(ref cases, fullRejected);

        GuideXosTextArea lineLimit = New("a\nb", 32, 2);
        bool lineRejected = lineLimit.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Rejected && lineLimit.Text == "a\nb" &&
            lineLimit.HistoryCount == 1 && !lineLimit.CanUndo;
        all &= Case(ref cases, lineRejected);

        GuideXosTextArea replaced = New("old");
        replaced.HandleCharacter('!');
        bool textReplacementResets = replaced.SetText("new") &&
            replaced.Text == "new" && replaced.HistoryCount == 1 &&
            !replaced.CanUndo && !replaced.CanRedo;
        all &= Case(ref cases, textReplacementResets);
        bool utf8ReplacementResets = replaced.HandleCharacter('!') ==
                GuideXosTextAreaEditResult.Changed && replaced.SetUtf8("loaded"u8) &&
            replaced.Text == "loaded" && replaced.HistoryCount == 1 &&
            !replaced.CanUndo && !replaced.CanRedo;
        all &= Case(ref cases, utf8ReplacementResets);

        GuideXosTextArea callbackArea = New("x");
        int callbackCount = 0;
        callbackArea.ContentChanged += () => ++callbackCount;
        callbackArea.HandleCharacter('y');
        int countAfterEdit = callbackCount;
        bool callbacksOnce = countAfterEdit == 1 && callbackArea.Undo() ==
                GuideXosTextAreaEditResult.Changed && callbackCount == 2 &&
            callbackArea.HistoryCount == 2 && callbackArea.Redo() ==
                GuideXosTextAreaEditResult.Changed && callbackCount == 3 &&
            callbackArea.HistoryCount == 2;
        all &= Case(ref cases, callbacksOnce);
        all &= Case(ref cases, callbackArea.Undo() ==
                GuideXosTextAreaEditResult.Changed &&
            callbackArea.Undo() == GuideXosTextAreaEditResult.Ignored &&
            callbackArea.Redo() == GuideXosTextAreaEditResult.Changed &&
            callbackArea.Redo() == GuideXosTextAreaEditResult.Ignored &&
            callbackCount == 5 && callbackArea.HistoryCount == 2);

        GuideXosTextArea noRecursion = New(string.Empty);
        noRecursion.HandleCharacter('a');
        noRecursion.HandleCharacter('b');
        int historyBeforeRestore = noRecursion.HistoryCount;
        noRecursion.Undo();
        bool restoreNoRecursion = noRecursion.HistoryCount == historyBeforeRestore &&
            noRecursion.Text == "a" && noRecursion.CanRedo;
        all &= Case(ref cases, restoreNoRecursion);

        GuideXosTextArea bounds = New("a\nb");
        bounds.HandleKey(GuideXosTextInputKey.Backspace);
        bool restoreBounds = bounds.Undo() == GuideXosTextAreaEditResult.Changed &&
            bounds.CaretIndex >= 0 && bounds.CaretIndex <= bounds.Length &&
            bounds.AnchorIndex >= 0 && bounds.AnchorIndex <= bounds.Length;
        all &= Case(ref cases, restoreBounds);

        GuideXosTextArea viewport = New("root", 256, 32, 4);
        for (int index = 0; index < 6; index++)
        {
            viewport.HandleKey(GuideXosTextInputKey.Enter);
        }
        viewport.HandleWheel(-8, 1);
        bool multiLineUndo = viewport.FirstVisibleLine > 0;
        for (int index = 0; index < 6; index++)
        {
            multiLineUndo &= viewport.Undo() == GuideXosTextAreaEditResult.Changed;
        }
        multiLineUndo &= viewport.Text == "root" && viewport.LineCount == 1 &&
            viewport.FirstVisibleLine == 0 &&
            viewport.FirstVisibleLine <= viewport.MaximumFirstVisibleLine;
        all &= Case(ref cases, multiLineUndo);

        GuideXosTextArea viewportOnly = New("a\nb\nc\nd\ne");
        int viewportOnlyHistory = viewportOnly.HistoryCount;
        viewportOnly.SetFirstVisibleLine(1);
        viewportOnly.HandleWheel(1);
        all &= Case(ref cases, viewportOnly.HistoryCount == viewportOnlyHistory &&
            !viewportOnly.CanUndo);

        GuideXosTextArea lifecycle = New("baseline");
        int lifecycleHistory = lifecycle.HistoryCount;
        lifecycle.Focus();
        lifecycle.Blur();
        lifecycle.SetVisible(false);
        lifecycle.SetVisible(true);
        lifecycle.ResetTransientState();
        all &= Case(ref cases, lifecycle.HistoryCount == lifecycleHistory &&
            !lifecycle.CanUndo && !lifecycle.CanRedo);

        GuideXosTextArea rejectedReplacement = New("keep");
        int rejectedHistory = rejectedReplacement.HistoryCount;
        GuideXosTextRevision rejectedRevision = rejectedReplacement.CurrentRevision;
        bool rejectedTextAtomic = !rejectedReplacement.SetText("bad\rtext") &&
            rejectedReplacement.Text == "keep" &&
            rejectedReplacement.HistoryCount == rejectedHistory &&
            rejectedReplacement.CurrentRevision == rejectedRevision;
        all &= Case(ref cases, rejectedTextAtomic);

        GuideXosTextArea repeated = New(string.Empty);
        repeated.HandleCharacter('a');
        repeated.HandleCharacter('a');
        repeated.HandleCharacter('a');
        bool repeatedKeys = repeated.HistoryCount == 4 &&
            repeated.Text == "aaa" && repeated.Undo() ==
                GuideXosTextAreaEditResult.Changed && repeated.Text == "aa";
        all &= Case(ref cases, repeatedKeys);

        GuideXosTextArea pointer = New("abc\ndef");
        int pointerHistory = pointer.HistoryCount;
        GuideXosTextRevision pointerRevision = pointer.CurrentRevision;
        GuideXosTextAreaEditResult pointerResult = pointer.HandlePointerDown(
            28, 72, 20, 72, 8, 18);
        all &= Case(ref cases, pointerResult == GuideXosTextAreaEditResult.Focused &&
            pointer.CaretIndex == 1 && pointer.HistoryCount == pointerHistory &&
            pointer.CurrentRevision == pointerRevision && !pointer.CanUndo);

        GuideXosTextArea invalidUtf8 = New("keep");
        int invalidUtf8History = invalidUtf8.HistoryCount;
        GuideXosTextRevision invalidUtf8Revision = invalidUtf8.CurrentRevision;
        bool invalidUtf8Atomic = !invalidUtf8.SetUtf8(new byte[257]) &&
            invalidUtf8.Text == "keep" &&
            invalidUtf8.HistoryCount == invalidUtf8History &&
            invalidUtf8.CurrentRevision == invalidUtf8Revision;
        all &= Case(ref cases, invalidUtf8Atomic);

        GuideXosTextArea stress = New(string.Empty);
        int stressCallbacks = 0;
        stress.ContentChanged += () => ++stressCallbacks;
        int expectedStressCallbacks = 0;
        bool stressValid = true;
        for (int index = 0; index < 100; index++)
        {
            GuideXosTextAreaEditResult mutation = stress.Length == 0
                ? stress.HandleCharacter('x')
                : stress.HandleKey(GuideXosTextInputKey.Backspace);
            stressValid &= mutation == GuideXosTextAreaEditResult.Changed;
            ++expectedStressCallbacks;
            if (index % 3 == 0)
            {
                stressValid &= stress.Undo() ==
                    GuideXosTextAreaEditResult.Changed;
                ++expectedStressCallbacks;
                stressValid &= stress.Redo() ==
                    GuideXosTextAreaEditResult.Changed;
                ++expectedStressCallbacks;
            }
            if (index % 7 == 0 && stress.CanUndo)
            {
                stressValid &= stress.Undo() ==
                    GuideXosTextAreaEditResult.Changed;
                ++expectedStressCallbacks;
            }
            stressValid &= stress.HistoryCount <= stress.HistoryCapacity &&
                stress.CaretIndex >= 0 && stress.CaretIndex <= stress.Length &&
                stress.AnchorIndex >= 0 && stress.AnchorIndex <= stress.Length &&
                stress.CurrentRevision.IsValid;
        }
        string newestStressText = stress.Text;
        int stressIndex = stress.HistoryIndex;
        int stressCount = stress.HistoryCount;
        stressValid &= stressCount == stress.HistoryCapacity &&
            stressIndex >= 0 && stressIndex < stressCount;
        while (stress.CanUndo)
        {
            stressValid &= stress.Undo() ==
                GuideXosTextAreaEditResult.Changed &&
                stress.CaretIndex >= 0 && stress.CaretIndex <= stress.Length &&
                stress.AnchorIndex >= 0 && stress.AnchorIndex <= stress.Length &&
                stress.HistoryCount == stressCount;
            ++expectedStressCallbacks;
        }
        stressValid &= !stress.CanUndo && stress.HistoryCount == stressCount;
        while (stress.CanRedo)
        {
            stressValid &= stress.Redo() ==
                GuideXosTextAreaEditResult.Changed &&
                stress.CaretIndex >= 0 && stress.CaretIndex <= stress.Length &&
                stress.AnchorIndex >= 0 && stress.AnchorIndex <= stress.Length &&
                stress.HistoryCount == stressCount;
            ++expectedStressCallbacks;
        }
        stressValid &= !stress.CanRedo && stress.Text == newestStressText &&
            stress.HistoryCount == stressCount &&
            stressCallbacks == expectedStressCallbacks;
        all &= Case(ref cases, stressValid);

        bool countCorrect = cases == CaseCount;
        host?.TryLog(all && countCorrect
            ? "C153-TEXT-AREA cases=25 insert=LF=Backspace=Delete=PASS stress=100 callbacks=once viewport=clamped result=PASS"u8
            : "C153-TEXT-AREA result=FAIL"u8);
        return all && countCorrect;
    }

    private static GuideXosTextArea New(string value,
        int maximumCharacters = 256, int maximumLines = 32,
        int visibleLines = 4)
    {
        GuideXosTextArea area = new(maximumCharacters, maximumLines,
            visibleLines, 32);
        if (!area.SetText(value)) return null;
        area.Focus();
        return area;
    }

    private static bool Case(ref int count, bool passed)
    {
        ++count;
        return passed;
    }
}
