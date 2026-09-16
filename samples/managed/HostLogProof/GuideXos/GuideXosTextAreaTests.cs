using System;

namespace HostLogProof;

/// <summary>
/// Deterministic, allocation-bounded contract probes for GuideXosTextArea.
/// The tests use the public control API; they do not reach into application
/// or compositor state.
/// </summary>
public static class GuideXosTextAreaTests
{
    public static bool Run(GuideXosHost host)
    {
        bool storage = StorageAndInsertion();
        bool editing = BoundaryEditing();
        bool navigation = Navigation();
        bool selection = Selection();
        bool bounds = CapacityAndBounds();
        bool viewport = Viewport();
        bool result = storage && editing && navigation && selection && bounds && viewport;

        if (host != null)
        {
            if (!storage) host.TryLog("C117-TEST-GROUP storage=FAIL"u8);
            if (!editing) host.TryLog("C117-TEST-GROUP editing=FAIL"u8);
            if (!navigation) host.TryLog("C117-TEST-GROUP navigation=FAIL"u8);
            if (!selection) host.TryLog("C117-TEST-GROUP selection=FAIL"u8);
            if (!bounds) host.TryLog("C117-TEST-GROUP bounds=FAIL"u8);
            if (!viewport) host.TryLog("C117-TEST-GROUP viewport=FAIL"u8);
            host.TryLog(result
                ? "C117-TESTS cases=42 storage=PASS editing=PASS navigation=PASS selection=PASS bounds=PASS viewport=PASS result=PASS"u8
                : "C117-TESTS cases=42 result=FAIL"u8);
        }
        return result;
    }

    private static bool StorageAndInsertion()
    {
        GuideXosTextArea empty = New(16, 4, "");
        bool emptyDocument = empty.Length == 0 && empty.LineCount == 1 &&
            empty.CaretIndex == 0 && !empty.HasSelection;
        GuideXosTextArea oneLine = New(16, 4, "one");
        bool oneLineDocument = oneLine.LineCount == 1 && oneLine.Text == "one";
        GuideXosTextArea multiline = New(32, 4, "a\nbb\nccc");
        bool multilineDocument = multiline.LineCount == 3 && multiline.Text == "a\nbb\nccc";

        GuideXosTextArea start = New(8, 4, "bc");
        start.SetCaretToStart();
        bool insertionStart = start.HandleCharacter('a') ==
            GuideXosTextAreaEditResult.Changed && start.Text == "abc";
        GuideXosTextArea end = New(8, 4, "ab");
        bool insertionEnd = end.HandleCharacter('c') ==
            GuideXosTextAreaEditResult.Changed && end.Text == "abc";
        GuideXosTextArea middle = New(8, 4, "ac");
        bool insertionMiddle = Move(middle, GuideXosTextInputKey.Left, 1) &&
            middle.HandleCharacter('b') == GuideXosTextAreaEditResult.Changed &&
            middle.Text == "abc";

        return emptyDocument && oneLineDocument && multilineDocument &&
            insertionStart && insertionEnd && insertionMiddle;
    }

    private static bool BoundaryEditing()
    {
        GuideXosTextArea split = New(16, 4, "ab");
        bool enter = Move(split, GuideXosTextInputKey.Left, 1) &&
            split.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && split.Text == "a\nb";

        GuideXosTextArea backspace = New(16, 4, "a\nb");
        bool joinedBackspace = Move(backspace, GuideXosTextInputKey.Home, 1) &&
            backspace.HandleKey(GuideXosTextInputKey.Backspace) ==
                GuideXosTextAreaEditResult.Changed && backspace.Text == "ab";

        GuideXosTextArea delete = New(16, 4, "a\nb");
        bool joinedDelete = Move(delete, GuideXosTextInputKey.Home, 1) &&
            Move(delete, GuideXosTextInputKey.Left, 1) &&
            delete.HandleKey(GuideXosTextInputKey.Delete) ==
                GuideXosTextAreaEditResult.Changed && delete.Text == "ab";

        GuideXosTextArea start = New(16, 4, "a");
        start.SetCaretToStart();
        bool repeatedBackspace = start.HandleKey(GuideXosTextInputKey.Backspace) ==
            GuideXosTextAreaEditResult.Ignored && start.CaretIndex == 0;
        GuideXosTextArea finish = New(16, 4, "a");
        bool repeatedDelete = finish.HandleKey(GuideXosTextInputKey.Delete) ==
            GuideXosTextAreaEditResult.Ignored && finish.CaretIndex == finish.Length;

        return enter && joinedBackspace && joinedDelete && repeatedBackspace &&
            repeatedDelete;
    }

    private static bool Navigation()
    {
        GuideXosTextArea left = New(16, 4, "a\nb");
        bool leftAcross = Move(left, GuideXosTextInputKey.Left, 2) &&
            left.CaretIndex == 1;
        GuideXosTextArea right = New(16, 4, "a\nb");
        right.SetCaretToStart();
        bool rightAcross = Move(right, GuideXosTextInputKey.Right, 2) &&
            right.CaretIndex == 2;

        GuideXosTextArea vertical = New(64, 8, "123456789012\nx\nabcd");
        vertical.SetCaretToStart();
        bool upUnequal = Move(vertical, GuideXosTextInputKey.Down, 2) &&
            Move(vertical, GuideXosTextInputKey.End, 1) &&
            Move(vertical, GuideXosTextInputKey.Up, 1) &&
            vertical.CaretColumn == 1;
        bool downUnequal = Move(vertical, GuideXosTextInputKey.Down, 1) &&
            vertical.CaretColumn == 4;
        GuideXosTextArea preferredArea = New(64, 8,
            "123456789012\nx\nabcdefghijklmnop");
        preferredArea.SetCaretToStart();
        bool preferred = Move(preferredArea, GuideXosTextInputKey.Right, 10) &&
            Move(preferredArea, GuideXosTextInputKey.Down, 2) &&
            preferredArea.CaretColumn == 10 && preferredArea.PreferredColumn == 10;

        GuideXosTextArea homeEnd = New(32, 8, "first\nsecond");
        homeEnd.SetCaretToStart();
        bool home = Move(homeEnd, GuideXosTextInputKey.Down, 1) &&
            Move(homeEnd, GuideXosTextInputKey.End, 1) &&
            Move(homeEnd, GuideXosTextInputKey.Home, 1) &&
            homeEnd.CaretIndex == 6 && homeEnd.CaretColumn == 0;
        bool end = Move(homeEnd, GuideXosTextInputKey.End, 1) &&
            homeEnd.CaretIndex == 12 && homeEnd.CaretColumn == 6;

        return leftAcross && rightAcross && upUnequal && downUnequal &&
            preferred && home && end;
    }

    private static bool Selection()
    {
        GuideXosTextArea forward = New(32, 4, "abcdef");
        forward.SetCaretToStart();
        bool selectionForward = Move(forward, GuideXosTextInputKey.Right, 3, true) &&
            forward.HasSelection && forward.AnchorIndex == 0 &&
            forward.CaretIndex == 3 && forward.SelectedText == "abc";

        GuideXosTextArea backward = New(32, 4, "abcdef");
        bool selectionBackward = Move(backward, GuideXosTextInputKey.Left, 2, true) &&
            backward.HasSelection && backward.AnchorIndex == 6 &&
            backward.CaretIndex == 4 && backward.SelectedText == "ef";

        GuideXosTextArea expansion = New(32, 4, "abcdef");
        expansion.SetCaretToStart();
        bool selectionExpansion = Move(expansion, GuideXosTextInputKey.Right, 2, true) &&
            Move(expansion, GuideXosTextInputKey.Right, 2, true) &&
            expansion.SelectionStart == 0 && expansion.SelectionEnd == 4;
        bool selectionContraction = Move(expansion, GuideXosTextInputKey.Left, 1, true) &&
            expansion.SelectionStart == 0 && expansion.SelectionEnd == 3;

        GuideXosTextArea reversal = New(32, 4, "abcdef");
        reversal.SetCaretToStart();
        bool selectionReversal = Move(reversal, GuideXosTextInputKey.Right, 4) &&
            Move(reversal, GuideXosTextInputKey.Right, 2, true) &&
            Move(reversal, GuideXosTextInputKey.Left, 4, true) &&
            reversal.AnchorIndex == 4 && reversal.CaretIndex == 2 &&
            reversal.SelectedText == "cd";

        GuideXosTextArea replacement = New(32, 4, "abcdef");
        replacement.SetCaretToStart();
        bool selectionReplacement = Move(replacement, GuideXosTextInputKey.Right, 3, true) &&
            replacement.HandleCharacter('X') == GuideXosTextAreaEditResult.Changed &&
            replacement.Text == "Xdef" && !replacement.HasSelection;

        GuideXosTextArea backspace = New(32, 4, "abcdef");
        backspace.SetCaretToStart();
        bool selectionBackspace = Move(backspace, GuideXosTextInputKey.Right, 2, true) &&
            backspace.HandleKey(GuideXosTextInputKey.Backspace) ==
                GuideXosTextAreaEditResult.Changed && backspace.Text == "cdef";
        GuideXosTextArea delete = New(32, 4, "abcdef");
        delete.SetCaretToStart();
        bool selectionDelete = Move(delete, GuideXosTextInputKey.Right, 2, true) &&
            delete.HandleKey(GuideXosTextInputKey.Delete) ==
                GuideXosTextAreaEditResult.Changed && delete.Text == "cdef";

        GuideXosTextArea collapseVertical = New(32, 4, "abc\ndef\nghi");
        collapseVertical.SetCaretToStart();
        bool selectionCollapseVertical = Move(collapseVertical,
                GuideXosTextInputKey.Right, 2, true) &&
            collapseVertical.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosTextAreaEditResult.Moved &&
            !collapseVertical.HasSelection && collapseVertical.CaretIndex == 2;

        GuideXosTextArea collapseHome = New(32, 4, "abcdef");
        collapseHome.SetCaretToStart();
        bool selectionCollapseHome = Move(collapseHome,
                GuideXosTextInputKey.Right, 3, true) &&
            collapseHome.HandleKey(GuideXosTextInputKey.Home) ==
                GuideXosTextAreaEditResult.Moved &&
            !collapseHome.HasSelection && collapseHome.CaretIndex == 0;

        GuideXosTextArea collapseEnd = New(32, 4, "abcdef");
        collapseEnd.SetCaretToStart();
        bool selectionCollapseEnd = Move(collapseEnd,
                GuideXosTextInputKey.Right, 3, true) &&
            collapseEnd.HandleKey(GuideXosTextInputKey.End) ==
                GuideXosTextAreaEditResult.Moved &&
            !collapseEnd.HasSelection && collapseEnd.CaretIndex == 3;

        GuideXosTextArea collapseLeft = New(32, 4, "abcdef");
        collapseLeft.SetCaretToStart();
        bool collapseByLeft = Move(collapseLeft, GuideXosTextInputKey.Right, 2, true) &&
            collapseLeft.HandleKey(GuideXosTextInputKey.Left) ==
                GuideXosTextAreaEditResult.Moved && collapseLeft.CaretIndex == 0 &&
            !collapseLeft.HasSelection;
        GuideXosTextArea collapseRight = New(32, 4, "abcdef");
        collapseRight.SetCaretToStart();
        bool collapseByRight = Move(collapseRight, GuideXosTextInputKey.Right, 2, true) &&
            collapseRight.HandleKey(GuideXosTextInputKey.Right) ==
                GuideXosTextAreaEditResult.Moved && collapseRight.CaretIndex == 2 &&
            !collapseRight.HasSelection;

        return selectionForward && selectionBackward && selectionExpansion &&
            selectionContraction && selectionReversal && selectionReplacement &&
            selectionBackspace && selectionDelete && selectionCollapseVertical &&
            selectionCollapseHome && selectionCollapseEnd && collapseByLeft &&
            collapseByRight;
    }

    private static bool CapacityAndBounds()
    {
        GuideXosTextArea capacity = New(5, 4, "abcde");
        string before = capacity.Text;
        bool rejectedOverflow = capacity.HandleCharacter('f') ==
            GuideXosTextAreaEditResult.Rejected && capacity.Text == before;

        GuideXosTextArea lineCapacity = New(16, 2, "a\nb");
        bool rejectedLineOverflow = lineCapacity.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosTextAreaEditResult.Rejected && lineCapacity.Text == "a\nb";

        GuideXosTextArea lineLoad = New(32, 2, "a\nb");
        int lineLoadCaret = lineLoad.CaretIndex;
        bool rejectedLineLoad = !lineLoad.SetText("a\nb\nc") &&
            lineLoad.Text == "a\nb" && lineLoad.LineCount == 2 &&
            lineLoad.CaretIndex == lineLoadCaret && !lineLoad.HasSelection;
        bool rejectedUtf8LineLoad = !lineLoad.SetUtf8("a\nb\nc"u8) &&
            lineLoad.Text == "a\nb" && lineLoad.LineCount == 2 &&
            lineLoad.CaretIndex == lineLoadCaret;

        GuideXosTextArea selectionBounds = New(32, 4, "abcdef");
        selectionBounds.SetCaretToStart();
        bool boundsAfterSelectionEdit = Move(selectionBounds,
                GuideXosTextInputKey.Right, 5, true) &&
            selectionBounds.HandleKey(GuideXosTextInputKey.Delete) ==
                GuideXosTextAreaEditResult.Changed && selectionBounds.CaretIndex == 0 &&
            selectionBounds.AnchorIndex == 0 && selectionBounds.SelectionStart == 0 &&
            selectionBounds.SelectionEnd == 0;

        GuideXosTextArea caretBounds = New(32, 4, "a\nb");
        caretBounds.SetCaretToStart();
        bool caretValid = Move(caretBounds, GuideXosTextInputKey.Right, 2) &&
            caretBounds.HandleKey(GuideXosTextInputKey.Backspace) ==
                GuideXosTextAreaEditResult.Changed && caretBounds.CaretIndex >= 0 &&
            caretBounds.CaretIndex <= caretBounds.Length;

        GuideXosTextArea focus = new(16, 4, 2, 12);
        bool focusIsolation = focus.HandleCharacter('x') ==
            GuideXosTextAreaEditResult.Ignored && focus.Text.Length == 0;
        focus.Focus();
        focus.Blur();
        focusIsolation &= focus.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosTextAreaEditResult.Ignored && focus.Text.Length == 0;

        return rejectedOverflow && rejectedLineOverflow && rejectedLineLoad &&
            rejectedUtf8LineLoad && boundsAfterSelectionEdit && caretValid &&
            focusIsolation;
    }

    private static bool Viewport()
    {
        GuideXosTextArea area = New(64, 8, "one\ntwo\nthree\nfour\nfive");
        area.SetCaretToStart();
        bool scrollDown = Move(area, GuideXosTextInputKey.Down, 4) &&
            area.FirstVisibleLine == 1 && area.CaretLine == 4;
        bool editScrolled = Move(area, GuideXosTextInputKey.End, 1) &&
            area.HandleCharacter('!') ==
            GuideXosTextAreaEditResult.Changed && area.Text.EndsWith("five!", StringComparison.Ordinal) &&
            area.FirstVisibleLine == 1;
        bool scrollUp = Move(area, GuideXosTextInputKey.Up, 4) &&
            area.FirstVisibleLine == 0 && area.CaretLine == 0;
        bool viewportValid = area.FirstVisibleLine >= 0 &&
            area.FirstVisibleLine <= area.LineCount - 1;
        return scrollDown && editScrolled && scrollUp && viewportValid;
    }

    private static GuideXosTextArea New(int maximumCharacters, int maximumLines,
        string value)
    {
        GuideXosTextArea area = new(maximumCharacters, maximumLines, 4, 48);
        if (!area.SetText(value)) return null;
        area.Focus();
        return area;
    }

    private static bool Move(
        GuideXosTextArea area,
        GuideXosTextInputKey key,
        int count,
        bool shift = false)
    {
        if (area == null) return false;
        for (int index = 0; index < count; index++)
        {
            GuideXosTextAreaEditResult result = area.HandleKey(key, shift);
            if (result == GuideXosTextAreaEditResult.Ignored ||
                result == GuideXosTextAreaEditResult.Rejected)
            {
                return false;
            }
        }
        return true;
    }
}
