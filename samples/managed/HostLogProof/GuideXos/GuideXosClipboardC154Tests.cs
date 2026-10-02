#if HOSTLOGPROOF_C154_MANAGED_CLIPBOARD
using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Bounded clipboard and TextArea mutation boundary proofs for C154.</summary>
public static class GuideXosClipboardC154Tests
{
    private static int s_cases;

    public static bool Run(GuideXosHost host)
    {
        s_cases = 0;
        GuideXosClipboard clipboard = GuideXosClipboard.Shared;
        clipboard.Clear();

        bool core = ClipboardCore(clipboard);
        bool copy = CopyCases(clipboard);
        bool cut = CutCases(clipboard);
        bool paste = PasteCases(clipboard);
        bool history = HistoryAndFileState(clipboard);
        bool menu = MenuCases(clipboard);
        bool stress = Stress(clipboard);
#if HOSTLOGPROOF_C135_POPUP_MENU_TESTS
        bool popupApi = GuideXosPopupMenuC135Tests.Run(host);
#else
        bool popupApi = true;
#endif
#if HOSTLOGPROOF_C135_HOST_TESTS
        bool popupHost = GuideXosPopupMenuC135HostTests.Run(host);
#else
        bool popupHost = true;
#endif

        clipboard.Clear();
        bool result = core && copy && cut && paste && history && menu && stress &&
            popupApi && popupHost;
        host.TryLog(core
            ? "C154-CLIPBOARD-CORE bounded=256 encoding=ascii-lf atomic=PASS result=PASS"u8
            : "C154-CLIPBOARD-CORE result=FAIL"u8);
        host.TryLog(cut
            ? "C154-CUT cases=PASS callbacks=once undo-redo=PASS result=PASS"u8
            : "C154-CUT result=FAIL"u8);
        host.TryLog(paste
            ? "C154-PASTE cases=PASS overflow=atomic line-limit=atomic result=PASS"u8
            : "C154-PASTE result=FAIL"u8);
        host.TryLog(history
            ? "C154-HISTORY copy=revision-free cut-paste=single-revision save-point=PASS result=PASS"u8
            : "C154-HISTORY result=FAIL"u8);
        host.TryLog(menu
            ?
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
                "C154-MENU capacity=13 dynamic-enabled=PASS repeated-open=PASS result=PASS"u8
#else
                "C154-MENU capacity=12 dynamic-enabled=PASS repeated-open=PASS result=PASS"u8
#endif
            : "C154-MENU result=FAIL"u8);
        host.TryLog(popupApi && popupHost
            ? "C135-FOCUSED-TESTS menu=PASS host=PASS result=PASS"u8
            : "C135-FOCUSED-TESTS menu=FAIL host=FAIL result=FAIL"u8);
        host.TryLog(StressResult
            ? "C154-STRESS operations=100 bounds=PASS history=PASS dirty=PASS result=PASS"u8
            : "C154-STRESS result=FAIL"u8);
        host.TryLog(result
            ? "C154-REGRESSIONS clipboard=PASS result=PASS"u8
            : "C154-REGRESSIONS clipboard=FAIL result=FAIL"u8);
        return result;
    }

    private static bool StressResult;

    private static bool ClipboardCore(GuideXosClipboard clipboard)
    {
        bool all = true;
        clipboard.Clear();
        all &= Check(!clipboard.HasText && clipboard.Length == 0);
        all &= Check(clipboard.TrySetText(ReadOnlySpan<byte>.Empty) &&
            !clipboard.HasText && clipboard.Length == 0);
        all &= Check(clipboard.TrySetText("plain ASCII"u8) &&
            clipboard.TextSpan.SequenceEqual("plain ASCII"u8));
        all &= Check(clipboard.TrySetText("first\nsecond"u8) &&
            clipboard.TextSpan.SequenceEqual("first\nsecond"u8));
        all &= Check(!clipboard.TrySetText("bad\ttext"u8) &&
            clipboard.TextSpan.SequenceEqual("first\nsecond"u8));
        Span<byte> nonAscii = stackalloc byte[] { 0x41, 0x80 };
        all &= Check(!clipboard.TrySetText(nonAscii) &&
            clipboard.TextSpan.SequenceEqual("first\nsecond"u8));
        Span<byte> maximum = stackalloc byte[GuideXosClipboard.Capacity];
        maximum.Fill((byte)'M');
        all &= Check(clipboard.TrySetText(maximum) &&
            clipboard.Length == GuideXosClipboard.Capacity && clipboard.HasText);
        Span<byte> oversized = stackalloc byte[GuideXosClipboard.Capacity + 1];
        oversized.Fill((byte)'X');
        all &= Check(!clipboard.TrySetText(oversized) &&
            clipboard.Length == GuideXosClipboard.Capacity &&
            clipboard.TextSpan.SequenceEqual(maximum));
        all &= Check(clipboard.TrySetText("new"u8) && clipboard.Length == 3 &&
            clipboard.TextSpan.SequenceEqual("new"u8));
        Span<byte> shortCopy = stackalloc byte[2];
        all &= Check(!clipboard.TryCopyText(shortCopy, out int shortWritten) &&
            shortWritten == 3 && clipboard.Length == 3);
        Span<byte> exactCopy = stackalloc byte[3];
        all &= Check(clipboard.TryCopyText(exactCopy, out int written) &&
            written == 3 && exactCopy.SequenceEqual("new"u8));
        clipboard.Clear();
        all &= Check(!clipboard.HasText && clipboard.Length == 0);
        return all;
    }

    private static bool CopyCases(GuideXosClipboard clipboard)
    {
        bool all = true;
        clipboard.TrySetText("keep"u8);
        GuideXosTextArea none = NewArea("abcdef");
        int noneHistory = none.HistoryCount;
        GuideXosTextRevision noneRevision = none.CurrentRevision;
        int noneCaret = none.CaretIndex;
        all &= Check(!none.TryCopySelectionTo(stackalloc byte[8], out _) &&
            clipboard.TextSpan.SequenceEqual("keep"u8));
        all &= Check(none.HistoryCount == noneHistory &&
            none.CurrentRevision == noneRevision && none.CaretIndex == noneCaret);

        GuideXosTextArea forward = NewArea("abcdef");
        Select(forward, 2, 4);
        int forwardHistory = forward.HistoryCount;
        GuideXosTextRevision forwardRevision = forward.CurrentRevision;
        int forwardCaret = forward.CaretIndex;
        int forwardAnchor = forward.AnchorIndex;
        int callbacks = 0;
        forward.ContentChanged += () => ++callbacks;
        all &= Check(clipboard.TryCopySelectionFrom(forward) &&
            clipboard.TextSpan.SequenceEqual("cd"u8));
        all &= Check(forward.Text == "abcdef" && forward.HasSelection &&
            forward.HistoryCount == forwardHistory &&
            forward.CurrentRevision == forwardRevision &&
            forward.CaretIndex == forwardCaret &&
            forward.AnchorIndex == forwardAnchor && callbacks == 0);

        GuideXosTextArea reverse = NewArea("abcdef");
        Select(reverse, 2, 4, reverse: true);
        Span<byte> reverseCopy = stackalloc byte[8];
        all &= Check(reverse.CaretIndex < reverse.AnchorIndex &&
            reverse.TryCopySelectionTo(reverseCopy, out int reverseLength) &&
            reverseLength == 2 && reverseCopy[..reverseLength].SequenceEqual("cd"u8));
        all &= Check(clipboard.TryCopySelectionFrom(reverse) &&
            clipboard.TextSpan.SequenceEqual("cd"u8));

        GuideXosTextArea single = NewArea("abc");
        Select(single, 1, 2);
        all &= Check(clipboard.TryCopySelectionFrom(single) &&
            clipboard.TextSpan.SequenceEqual("b"u8));

        GuideXosTextArea whole = NewArea("whole");
        Select(whole, 0, whole.Length);
        all &= Check(clipboard.TryCopySelectionFrom(whole) &&
            clipboard.TextSpan.SequenceEqual("whole"u8));

        GuideXosTextArea multi = NewArea("one\ntwo\nthree");
        Select(multi, 2, 9);
        all &= Check(clipboard.TryCopySelectionFrom(multi) &&
            clipboard.TextSpan.SequenceEqual("e\ntwo\nt"u8));

        GuideXosTextArea maximum = NewArea(new string('A', 256));
        Select(maximum, 0, maximum.Length);
        all &= Check(clipboard.TryCopySelectionFrom(maximum) &&
            clipboard.Length == GuideXosClipboard.Capacity);
        return all;
    }

    private static bool CutCases(GuideXosClipboard clipboard)
    {
        bool all = true;
        clipboard.TrySetText("keep"u8);
        GuideXosTextArea none = NewArea("abcdef");
        int noneHistory = none.HistoryCount;
        GuideXosTextRevision noneRevision = none.CurrentRevision;
        all &= Check(none.CutSelection(clipboard) ==
                GuideXosTextAreaEditResult.Ignored && none.Text == "abcdef" &&
            none.HistoryCount == noneHistory && none.CurrentRevision == noneRevision &&
            clipboard.TextSpan.SequenceEqual("keep"u8));

        GuideXosTextArea beginning = NewArea("abcdef");
        Select(beginning, 0, 2);
        all &= Check(CutOnce(beginning, clipboard, "ab"u8, "cdef"));

        GuideXosTextArea middle = NewArea("abcdef");
        Select(middle, 2, 4);
        int callbacks = 0;
        middle.ContentChanged += () => ++callbacks;
        int beforeHistory = middle.HistoryCount;
        all &= Check(middle.CutSelection(clipboard) ==
                GuideXosTextAreaEditResult.Changed && middle.Text == "abef" &&
            clipboard.TextSpan.SequenceEqual("cd"u8) && !middle.HasSelection &&
            middle.CaretIndex == 2 && middle.HistoryCount == beforeHistory + 1 &&
            callbacks == 1);
        all &= Check(middle.Undo() == GuideXosTextAreaEditResult.Changed &&
            middle.Text == "abcdef" && clipboard.TextSpan.SequenceEqual("cd"u8));
        all &= Check(middle.Redo() == GuideXosTextAreaEditResult.Changed &&
            middle.Text == "abef" && clipboard.TextSpan.SequenceEqual("cd"u8));

        GuideXosTextArea end = NewArea("abcdef");
        Select(end, 4, 6);
        all &= Check(CutOnce(end, clipboard, "ef"u8, "abcd"));

        GuideXosTextArea whole = NewArea("all");
        Select(whole, 0, whole.Length);
        all &= Check(CutOnce(whole, clipboard, "all"u8, string.Empty));

        GuideXosTextArea multiline = NewArea("one\ntwo\nthree");
        Select(multiline, 2, 9, reverse: true);
        all &= Check(CutOnce(multiline, clipboard, "e\ntwo\nt"u8, "onhree"));

        GuideXosTextArea tooLarge = NewArea(new string('Z', 257), 1024);
        Select(tooLarge, 0, tooLarge.Length);
        clipboard.TrySetText("preserve"u8);
        int tooLargeHistory = tooLarge.HistoryCount;
        GuideXosTextRevision tooLargeRevision = tooLarge.CurrentRevision;
        all &= Check(tooLarge.CutSelection(clipboard) ==
                GuideXosTextAreaEditResult.Rejected && tooLarge.Length == 257 &&
            tooLarge.HistoryCount == tooLargeHistory &&
            tooLarge.CurrentRevision == tooLargeRevision &&
            clipboard.TextSpan.SequenceEqual("preserve"u8));
        return all;
    }

    private static bool PasteCases(GuideXosClipboard clipboard)
    {
        bool all = true;
        clipboard.Clear();
        GuideXosTextArea empty = NewArea("abc");
        int emptyHistory = empty.HistoryCount;
        GuideXosTextRevision emptyRevision = empty.CurrentRevision;
        all &= Check(empty.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Ignored && empty.Text == "abc" &&
            empty.HistoryCount == emptyHistory &&
            empty.CurrentRevision == emptyRevision);

        clipboard.TrySetText("X"u8);
        GuideXosTextArea emptyDoc = NewArea(string.Empty);
        all &= Check(emptyDoc.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Changed && emptyDoc.Text == "X" &&
            emptyDoc.CaretIndex == 1 && !emptyDoc.HasSelection);

        GuideXosTextArea beginning = NewArea("abc");
        beginning.SetCaretToStart();
        all &= Check(PasteOnce(beginning, clipboard, "Xabc"));

        GuideXosTextArea middle = NewArea("abcd");
        MoveTo(middle, 2);
        all &= Check(PasteOnce(middle, clipboard, "abXcd"));

        GuideXosTextArea end = NewArea("abc");
        end.SetCaretToStart();
        MoveTo(end, end.Length);
        all &= Check(PasteOnce(end, clipboard, "abcX"));

        GuideXosTextArea replace = NewArea("abcdef");
        Select(replace, 2, 4, reverse: true);
        int callbacks = 0;
        replace.ContentChanged += () => ++callbacks;
        int replaceHistory = replace.HistoryCount;
        GuideXosTextRevision replaceBefore = replace.CurrentRevision;
        all &= Check(replace.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Changed && replace.Text == "abXef" &&
            !replace.HasSelection && replace.CaretIndex == 3 && callbacks == 1 &&
            replace.HistoryCount == replaceHistory + 1 &&
            replace.CurrentRevision != replaceBefore);
        all &= Check(replace.Undo() == GuideXosTextAreaEditResult.Changed &&
            replace.Text == "abcdef" && replace.HasSelection &&
            replace.CaretIndex == 2 && replace.AnchorIndex == 4 &&
            clipboard.TextSpan.SequenceEqual("X"u8));
        all &= Check(replace.Redo() == GuideXosTextAreaEditResult.Changed &&
            replace.Text == "abXef" && clipboard.TextSpan.SequenceEqual("X"u8));
        all &= Check(replace.PasteText(clipboard.TextSpan) ==
            GuideXosTextAreaEditResult.Changed && replace.Text == "abXXef");

        clipboard.TrySetText("\nB"u8);
        GuideXosTextArea multiline = NewArea("A\nC");
        MoveTo(multiline, 1);
        all &= Check(multiline.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Changed && multiline.Text == "A\nB\nC" &&
            multiline.LineCount == 3 && multiline.CaretIndex == 3);

        clipboard.TrySetText("X"u8);
        GuideXosTextArea exactFit = NewArea(new string('A', 255));
        MoveTo(exactFit, 128);
        all &= Check(exactFit.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Changed && exactFit.Length == 256 &&
            exactFit.Text[128] == 'X');

        GuideXosTextArea overflow = NewArea(new string('A', 256));
        MoveTo(overflow, 64);
        int overflowHistory = overflow.HistoryCount;
        GuideXosTextRevision overflowRevision = overflow.CurrentRevision;
        int overflowCaret = overflow.CaretIndex;
        int overflowAnchor = overflow.AnchorIndex;
        callbacks = 0;
        overflow.ContentChanged += () => ++callbacks;
        all &= Check(!overflow.CanPasteText(clipboard.TextSpan) &&
            overflow.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Rejected && overflow.Length == 256 &&
            overflow.HistoryCount == overflowHistory &&
            overflow.CurrentRevision == overflowRevision &&
            overflow.CaretIndex == overflowCaret &&
            overflow.AnchorIndex == overflowAnchor && callbacks == 0 &&
            clipboard.TextSpan.SequenceEqual("X"u8));

        GuideXosTextArea lineOverflow = NewArea("a", 256, 2);
        clipboard.TrySetText("\nB\nC"u8);
        int lineHistory = lineOverflow.HistoryCount;
        GuideXosTextRevision lineRevision = lineOverflow.CurrentRevision;
        all &= Check(!lineOverflow.CanPasteText(clipboard.TextSpan) &&
            lineOverflow.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Rejected && lineOverflow.Text == "a" &&
            lineOverflow.LineCount == 1 && lineOverflow.HistoryCount == lineHistory &&
            lineOverflow.CurrentRevision == lineRevision);
        return all;
    }

    private static bool HistoryAndFileState(GuideXosClipboard clipboard)
    {
        bool all = true;
        GuideXosTextArea area = NewArea("abcd");
        GuideXosNotesDocumentState state = new();
        all &= Check(state.TryHydrate(area, "/system/apps/C154/a.txt", "abcd"u8) &&
            !state.Dirty);
        Select(area, 1, 3);
        GuideXosTextRevision copyRevision = area.CurrentRevision;
        int copyHistory = area.HistoryCount;
        clipboard.TryCopySelectionFrom(area);
        all &= Check(!state.Dirty && area.HistoryCount == copyHistory &&
            area.CurrentRevision == copyRevision);
        all &= Check(area.CutSelection(clipboard) ==
                GuideXosTextAreaEditResult.Changed && state.Dirty &&
            area.Text == "ad");
        all &= Check(area.Undo() == GuideXosTextAreaEditResult.Changed &&
            !state.Dirty && area.Text == "abcd" &&
            clipboard.TextSpan.SequenceEqual("bc"u8));
        all &= Check(area.Redo() == GuideXosTextAreaEditResult.Changed &&
            state.Dirty && area.Text == "ad" &&
            clipboard.TextSpan.SequenceEqual("bc"u8));
        all &= Check(state.MarkSaveSucceeded("/system/apps/C154/copy.txt") &&
            !state.Dirty && area.HistoryCount == 2 &&
            clipboard.TextSpan.SequenceEqual("bc"u8));
        all &= Check(area.Undo() == GuideXosTextAreaEditResult.Changed &&
            state.Dirty && area.Text == "abcd");
        all &= Check(area.Redo() == GuideXosTextAreaEditResult.Changed &&
            !state.Dirty && area.Text == "ad");

        clipboard.TrySetText("A\nB"u8);
        GuideXosTextArea second = NewArea("target");
        GuideXosNotesDocumentState secondState = new();
        all &= Check(secondState.TryHydrate(second,
            "/system/apps/C154/b.txt", "target"u8) &&
            clipboard.TextSpan.SequenceEqual("A\nB"u8));
        MoveTo(second, 3);
        all &= Check(second.PasteText(clipboard.TextSpan) ==
                GuideXosTextAreaEditResult.Changed && second.Text == "tarA\nBget" &&
            secondState.Dirty);
        all &= Check(secondState.MarkSaveSucceeded(
                "/system/apps/C154/b-copy.txt") && !secondState.Dirty &&
            clipboard.TextSpan.SequenceEqual("A\nB"u8));
        all &= Check(second.Undo() == GuideXosTextAreaEditResult.Changed &&
            secondState.Dirty && secondState.CurrentPath ==
                "/system/apps/C154/b-copy.txt");
        all &= Check(second.Redo() == GuideXosTextAreaEditResult.Changed &&
            !secondState.Dirty && clipboard.TextSpan.SequenceEqual("A\nB"u8));

        GuideXosTextArea branch = NewArea("abc");
        MoveTo(branch, 3);
        branch.PasteText("X"u8);
        branch.Undo();
        branch.HandleCharacter('Y');
        all &= Check(branch.Text == "abcY" && !branch.CanRedo &&
            branch.Redo() == GuideXosTextAreaEditResult.Ignored);
        return all;
    }

    private static bool MenuCases(GuideXosClipboard clipboard)
    {
        bool all = true;
        GuideXosPopupMenu menu = NewMenu();
        GuideXosTextArea area = NewArea("abc");
        clipboard.Clear();
        int history = area.HistoryCount;
        GuideXosTextRevision revision = area.CurrentRevision;
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
        all &= Check(menu.MaximumItemCount == 13 && menu.ItemCount == 13 &&
            menu.GetItemText(0) == "New" && menu.GetItemText(1) == "Open" &&
            menu.GetItemText(2) == "Save" && menu.GetItemText(3) == "Save As..." &&
            menu.IsSeparator(4) && menu.GetItemText(5) == "Undo" &&
            menu.GetItemText(6) == "Redo" && menu.IsSeparator(7) &&
            menu.GetItemText(8) == "Cut" && menu.GetItemText(9) == "Copy" &&
            menu.GetItemText(10) == "Paste" && menu.IsSeparator(11) &&
            menu.GetItemText(12) == "Reload");
#else
        all &= Check(menu.MaximumItemCount == 12 && menu.ItemCount == 12 &&
            menu.GetItemText(0) == "Undo" && menu.GetItemText(1) == "Redo" &&
            menu.IsSeparator(2) && menu.GetItemText(3) == "Cut" &&
            menu.GetItemText(4) == "Copy" && menu.GetItemText(5) == "Paste" &&
            menu.IsSeparator(6) && menu.GetItemText(7) == "Open" &&
            menu.GetItemText(11) == "Reload");
#endif
        all &= Check(ManagedNotes.RefreshC154MenuStates(menu, area, clipboard) &&
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            !menu.IsItemEnabled(5) && !menu.IsItemEnabled(6) &&
            !menu.IsItemEnabled(8) && !menu.IsItemEnabled(9) &&
            !menu.IsItemEnabled(10));
#else
            !menu.IsItemEnabled(0) && !menu.IsItemEnabled(1) &&
            !menu.IsItemEnabled(3) && !menu.IsItemEnabled(4) &&
            !menu.IsItemEnabled(5));
#endif
        all &= Check(area.HistoryCount == history &&
            area.CurrentRevision == revision && !area.HasSelection);

        Select(area, 0, 2);
        all &= Check(ManagedNotes.RefreshC154MenuStates(menu, area, clipboard) &&
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            menu.IsItemEnabled(8) && menu.IsItemEnabled(9) &&
            !menu.IsItemEnabled(10));
#else
            menu.IsItemEnabled(3) && menu.IsItemEnabled(4) &&
            !menu.IsItemEnabled(5));
#endif
        clipboard.TrySetText("Z"u8);
        all &= Check(ManagedNotes.RefreshC154MenuStates(menu, area, clipboard) &&
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            menu.IsItemEnabled(10));
#else
            menu.IsItemEnabled(5));
#endif

        GuideXosTextArea full = NewArea(new string('A', 256));
        clipboard.TrySetText("X"u8);
        all &= Check(ManagedNotes.RefreshC154MenuStates(menu, full, clipboard) &&
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            !menu.IsItemEnabled(10));
#else
            !menu.IsItemEnabled(5));
#endif

        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        all &= Check(menu.Open() == GuideXosPopupMenuResult.Opened &&
            menu.HandlePointerDown(menu.X + 2,
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
                menu.Y + 8 * GuideXosPopupMenu.PopupRowHeight + 1) ==
#else
                menu.Y + 3 * GuideXosPopupMenu.PopupRowHeight + 1) ==
#endif
                GuideXosPopupMenuResult.Disabled && callbacks == 0 && menu.IsOpen);
        menu.Cancel();
        for (int index = 0; index < 25; index++)
        {
            if (!ManagedNotes.RefreshC154MenuStates(menu, area, clipboard) ||
                menu.Open() != GuideXosPopupMenuResult.Opened ||
                menu.Cancel() != GuideXosPopupMenuResult.Cancelled)
            {
                all = false;
                break;
            }
        }
        all &= Check(menu.ItemCount ==
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            13
#else
            12
#endif
            && callbacks == 0 && !menu.IsOpen);
        return all;
    }

    private static bool Stress(GuideXosClipboard clipboard)
    {
        GuideXosTextArea area = NewArea("seed");
        GuideXosNotesDocumentState state = new();
        state.TryHydrate(area, "/system/apps/C154/stress.txt", "seed"u8);
        clipboard.Clear();
        bool valid = true;
        for (int index = 0; index < 100; index++)
        {
            switch (index % 6)
            {
                case 0:
                    if (area.Length > 0)
                    {
                        int start = index % area.Length;
                        Select(area, start, start + 1);
                        clipboard.TryCopySelectionFrom(area);
                    }
                    break;
                case 1:
                    if (clipboard.HasText && area.CanPasteText(clipboard.TextSpan))
                        area.PasteText(clipboard.TextSpan);
                    break;
                case 2:
                    if (area.Length > 0)
                    {
                        int start = (index / 2) % area.Length;
                        Select(area, start, start + 1);
                        area.CutSelection(clipboard);
                    }
                    break;
                case 3:
                    if (area.CanUndo) area.Undo();
                    break;
                case 4:
                    if (area.CanRedo) area.Redo();
                    break;
                default:
                    area.SetCaretToStart();
                    area.HandleKey(GuideXosTextInputKey.Right);
                    break;
            }
            bool bounds = area.Length <= area.MaximumCharacters &&
                area.LineCount <= area.MaximumLines &&
                area.CaretIndex >= 0 && area.CaretIndex <= area.Length &&
                area.AnchorIndex >= 0 && area.AnchorIndex <= area.Length &&
                area.SelectionStart >= 0 && area.SelectionEnd <= area.Length &&
                area.HistoryCount <= area.HistoryCapacity &&
                clipboard.Length <= GuideXosClipboard.Capacity &&
                state.Dirty == (!area.IsRevisionReachable(state.SavedRevision) ||
                    area.CurrentRevision != state.SavedRevision);
            valid &= bounds;
            if (!bounds) break;
        }
        StressResult = valid;
        return Check(valid);
    }

    private static bool CutOnce(GuideXosTextArea area,
        GuideXosClipboard clipboard, ReadOnlySpan<byte> expectedClipboard,
        string expectedText)
    {
        int before = area.HistoryCount;
        int callbacks = 0;
        area.ContentChanged += () => ++callbacks;
        GuideXosTextAreaEditResult result = area.CutSelection(clipboard);
        return result == GuideXosTextAreaEditResult.Changed &&
            area.Text == expectedText && clipboard.TextSpan.SequenceEqual(expectedClipboard) &&
            !area.HasSelection && area.HistoryCount == before + 1 && callbacks == 1;
    }

    private static bool PasteOnce(GuideXosTextArea area,
        GuideXosClipboard clipboard, string expectedText)
    {
        int before = area.HistoryCount;
        int callbacks = 0;
        area.ContentChanged += () => ++callbacks;
        GuideXosTextAreaEditResult result = area.PasteText(clipboard.TextSpan);
        return result == GuideXosTextAreaEditResult.Changed &&
            area.Text == expectedText && !area.HasSelection &&
            area.HistoryCount == before + 1 && callbacks == 1 &&
            area.CaretIndex == expectedText.IndexOf('X') + 1;
    }

    private static GuideXosTextArea NewArea(string text,
        int maximumCharacters = 256, int maximumLines = 32)
    {
        GuideXosTextArea area = new(maximumCharacters, maximumLines);
        area.SetText(text);
        area.Focus();
        return area;
    }

    private static void MoveTo(GuideXosTextArea area, int position)
    {
        area.SetCaretToStart();
        for (int index = 0; index < position; index++)
            area.HandleKey(GuideXosTextInputKey.Right);
    }

    private static void Select(GuideXosTextArea area, int start, int end,
        bool reverse = false)
    {
        MoveTo(area, reverse ? end : start);
        for (int index = start; index < end; index++)
            area.HandleKey(reverse ? GuideXosTextInputKey.Left :
                GuideXosTextInputKey.Right, true);
    }

    private static GuideXosPopupMenu NewMenu()
    {
        GuideXosPopupMenu menu = new(340, 40, 176,
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
            13,
#else
            12,
#endif
            32);
#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
        menu.TryAddItem("New", 25u);
        menu.TryAddItem("Open", 20u);
        menu.TryAddItem("Save", 22u);
        menu.TryAddItem("Save As...", 21u);
        menu.TryAddSeparator();
        menu.TryAddItem("Undo", 0xC15301u);
        menu.TryAddItem("Redo", 0xC15302u);
        menu.TryAddSeparator();
        menu.TryAddItem("Cut", ManagedNotes.C154CutActionId);
        menu.TryAddItem("Copy", ManagedNotes.C154CopyActionId);
        menu.TryAddItem("Paste", ManagedNotes.C154PasteActionId);
        menu.TryAddSeparator();
        menu.TryAddItem("Reload", 3u);
#else
        menu.TryAddItem("Undo", 0xC15301u);
        menu.TryAddItem("Redo", 0xC15302u);
        menu.TryAddSeparator();
        menu.TryAddItem("Cut", ManagedNotes.C154CutActionId);
        menu.TryAddItem("Copy", ManagedNotes.C154CopyActionId);
        menu.TryAddItem("Paste", ManagedNotes.C154PasteActionId);
        menu.TryAddSeparator();
        menu.TryAddItem("Open", 20u);
        menu.TryAddItem("Save", 22u);
        menu.TryAddItem("Save As...", 21u);
        menu.TryAddSeparator();
        menu.TryAddItem("Reload", 3u);
#endif
        return menu;
    }

    private static bool Check(bool condition)
    {
        ++s_cases;
        return condition;
    }
}
#endif
