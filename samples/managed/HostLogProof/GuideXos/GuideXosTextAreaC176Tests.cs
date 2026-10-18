#if HOSTLOGPROOF_C176_MANAGED_NOTES_NAVIGATION
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosTextAreaC176Tests
{
    public const int CaseCount = 20;
    public const int StressCaseCount = 7;
    public const int StressNavigationOperations = 1000;
    public const int StressAlternatingResizes = 500;
    public const int StressNavigationResizeOperations = 250;
    public const int StressRejectedGeometryRequests = 250;
    public const int StressLifecycleCycles = 100;

    public static int FirstFailedCase { get; private set; }
    public static int NavigationOperationCount { get; private set; }
    public static int AlternatingResizeCount { get; private set; }
    public static int NavigationResizeCount { get; private set; }
    public static int RejectedGeometryCount { get; private set; }
    public static int LifecycleCount { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }
    public static int DriftCount { get; private set; }

    private static ReadOnlySpan<byte> MixedDocument =>
        "abcdef\r\nx\n12345\r\nabcdefghij"u8;

    private static ReadOnlySpan<byte> StressDocument =>
        "line-00-long-content\r\nline-01\nline-02-medium\r\nline-03\nline-04-long-content\r\nline-05\nline-06-medium\r\nline-07\nline-08-long-content\r\nline-09\nline-10-medium\r\nline-11"u8;

    public static bool Run(GuideXosHost host)
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases, EmptyDocument());
        passed &= Case(ref cases, LfWithoutTrailingNewline());
        passed &= Case(ref cases, LfTrailingNewline());
        passed &= Case(ref cases, CrLfWithoutTrailingNewline());
        passed &= Case(ref cases, BlankAndConsecutiveLines());
        passed &= Case(ref cases, MixedLineEndingsRemainExact());
        passed &= Case(ref cases, CrOnlyRejectedAtomically());
        passed &= Case(ref cases, SetTextSupportsCrLf());
        passed &= Case(ref cases, HorizontalMovementSkipsCrLfPair());
        passed &= Case(ref cases, VerticalPreferredColumn());
        passed &= Case(ref cases, HomeAndEndUseLogicalLineEdges());
        passed &= Case(ref cases, SelectionCanCoverWholeDelimiter());
        passed &= Case(ref cases, VerticalScrollingKeepsCaretVisible());
        passed &= Case(ref cases, LongLinesRemainNonWrapping());
        passed &= Case(ref cases, BackspaceRemovesCrLfAtomically());
        passed &= Case(ref cases, DeleteRemovesCrLfAtomically());
        passed &= Case(ref cases, EnterAndHistoryKeepValidBoundaries());
        passed &= Case(ref cases, CopyAndPastePreserveCrLf());
        passed &= Case(ref cases, ResizePreservesEditorState());
        passed &= Case(ref cases, MixedDocumentHydratesAndSerializesExactly());

        if (cases != CaseCount) passed = false;
        host?.TryLog(passed
            ? "C176-NOTES-NAVIGATION focused=20 result=PASS"u8
            : "C176-NOTES-NAVIGATION result=FAIL"u8);
        host?.TryLog(FirstFailedCase == 0
            ? "C176-NOTES-NAVIGATION-DETAIL firstFailedCase=0"u8
            : "C176-NOTES-NAVIGATION-DETAIL firstFailedCase=nonzero"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        NavigationOperationCount = 0;
        AlternatingResizeCount = 0;
        NavigationResizeCount = 0;
        RejectedGeometryCount = 0;
        LifecycleCount = 0;
        ResizeAllocationBytes = -1;
        DriftCount = 0;

        if (!CreateDocument(StressDocument, "/system/apps/C176/stress.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }

        area.Focus();
        area.SetCaretToStart();
        Span<byte> expected = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        if (!area.TryCopyUtf8To(expected, out int expectedLength)) return false;
        Span<byte> observed = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        GuideXosTextInputKey[] keys =
        [
            GuideXosTextInputKey.Right,
            GuideXosTextInputKey.Down,
            GuideXosTextInputKey.Left,
            GuideXosTextInputKey.Down,
            GuideXosTextInputKey.Home,
            GuideXosTextInputKey.Right,
            GuideXosTextInputKey.Up,
            GuideXosTextInputKey.End,
        ];

        for (int index = 0; index < StressNavigationOperations; index++)
        {
            GuideXosTextAreaEditResult result = area.HandleKey(
                keys[index & 7], shift: index % 11 == 0);
            if (result is not (GuideXosTextAreaEditResult.Moved or
                    GuideXosTextAreaEditResult.Ignored) ||
                !IsValidEditorState(area, observed, out int observedLength) ||
                observedLength != expectedLength ||
                !observed[..observedLength].SequenceEqual(
                    expected[..expectedLength]))
            {
                ++DriftCount;
            }
            ++NavigationOperationCount;
        }

        int width = 600;
        int height = 360;
        if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 560, 344) ||
            !GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 960, 436))
        {
            ++DriftCount;
            return false;
        }

        long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < StressAlternatingResizes; index++)
        {
            bool minimum = (index & 1) == 0;
            int targetWidth = minimum ? 560 : 960;
            int targetHeight = minimum ? 344 : 436;
            if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(
                    area, document, ref width, ref height, targetWidth,
                    targetHeight) || width != targetWidth ||
                height != targetHeight)
            {
                ++DriftCount;
            }
            ++AlternatingResizeCount;
        }

        for (int index = 0; index < StressNavigationResizeOperations; index++)
        {
            GuideXosTextInputKey key = keys[index & 7];
            GuideXosTextAreaEditResult move = area.HandleKey(key,
                shift: index % 9 == 0);
            bool minimum = (index & 1) == 0;
            int targetWidth = minimum ? 560 : 960;
            int targetHeight = minimum ? 344 : 436;
            if (move is not (GuideXosTextAreaEditResult.Moved or
                    GuideXosTextAreaEditResult.Ignored) ||
                !GuideXosManagedWindowResizeC175Tests.TryResizeForC176(
                    area, document, ref width, ref height, targetWidth,
                    targetHeight) ||
                !IsValidEditorState(area, observed, out int observedLength) ||
                observedLength != expectedLength ||
                !observed[..observedLength].SequenceEqual(
                    expected[..expectedLength]))
            {
                ++DriftCount;
            }
            ++NavigationResizeCount;
        }

        for (int index = 0; index < StressRejectedGeometryRequests; index++)
        {
            int priorWidth = width;
            int priorHeight = height;
            if (GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                    document, ref width, ref height, 559, priorHeight) ||
                width != priorWidth || height != priorHeight ||
                !IsValidEditorState(area, observed, out int observedLength) ||
                observedLength != expectedLength ||
                !observed[..observedLength].SequenceEqual(
                    expected[..expectedLength]))
            {
                ++DriftCount;
            }
            ++RejectedGeometryCount;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocatedBefore;

        for (int cycle = 0; cycle < StressLifecycleCycles; cycle++)
        {
            ManagedNotes notes = new();
            ulong identity = (ulong)(0xC1760000u + (uint)cycle + 1u);
            if (!notes.BeginC175LifecycleForTests(identity) ||
                notes.C175WindowForTests != identity ||
                !notes.ResizeC175ForTests(960, 436) ||
                !notes.ResizeC175ForTests(560, 344) ||
                notes.C175WindowForTests != identity ||
                !notes.CloseC175LifecycleForTests() ||
                notes.C175WindowForTests != 0u)
            {
                ++DriftCount;
            }
            else
            {
                ++LifecycleCount;
            }
        }

        bool passed = NavigationOperationCount == StressNavigationOperations &&
            AlternatingResizeCount == StressAlternatingResizes &&
            NavigationResizeCount == StressNavigationResizeOperations &&
            RejectedGeometryCount == StressRejectedGeometryRequests &&
            LifecycleCount == StressLifecycleCycles &&
            ResizeAllocationBytes == 0 && DriftCount == 0 &&
            width == 960 && height == 436;
        host?.TryLog(passed
            ? "C176-NOTES-NAVIGATION-STRESS navigation=1000 alternating=500 navigationResize=250 rejected=250 lifecycle=100 allocationBytes=0 drift=0 result=PASS"u8
            : "C176-NOTES-NAVIGATION-STRESS result=FAIL"u8);
        return passed;
    }

    private static bool EmptyDocument()
    {
        GuideXosTextArea area = new();
        return area.SetUtf8(ReadOnlySpan<byte>.Empty) && area.LineCount == 1 &&
            area.Length == 0 && area.CaretIndex == 0 && area.CaretLine == 0 &&
            area.CaretColumn == 0;
    }

    private static bool LfWithoutTrailingNewline()
    {
        GuideXosTextArea area = new();
        return area.SetUtf8("short\nlonger"u8) && area.LineCount == 2 &&
            area.Length == 12 && area.CaretLine == 1 &&
            area.CaretColumn == 6 &&
            area.ToUtf8().AsSpan().SequenceEqual("short\nlonger"u8);
    }

    private static bool LfTrailingNewline()
    {
        GuideXosTextArea area = new();
        return area.SetUtf8("tail\n"u8) && area.LineCount == 2 &&
            area.CaretIndex == 5 && area.CaretLine == 1 &&
            area.CaretColumn == 0 && GetLineLength("tail\n"u8, 1) == 0;
    }

    private static bool CrLfWithoutTrailingNewline()
    {
        ReadOnlySpan<byte> bytes = "short\r\nlonger"u8;
        GuideXosTextArea area = new();
        return area.SetUtf8(bytes) && area.LineCount == 2 &&
            area.Length == bytes.Length && area.CaretLine == 1 &&
            area.CaretColumn == 6 &&
            area.ToUtf8().AsSpan().SequenceEqual(bytes);
    }

    private static bool BlankAndConsecutiveLines()
    {
        ReadOnlySpan<byte> bytes = "a\r\n\r\nb\n\n"u8;
        GuideXosTextArea area = new();
        return area.SetUtf8(bytes) && area.LineCount == 5 &&
            GetLineLength(bytes, 0) == 1 && GetLineLength(bytes, 1) == 0 &&
            GetLineLength(bytes, 2) == 1 && GetLineLength(bytes, 3) == 0 &&
            GetLineLength(bytes, 4) == 0 &&
            area.ToUtf8().AsSpan().SequenceEqual(bytes);
    }

    private static bool MixedLineEndingsRemainExact()
    {
        GuideXosTextArea area = new(256, 32, 4, 32);
        if (!area.SetUtf8(MixedDocument) || area.LineCount != 4) return false;
        area.Focus();
        area.SetCaretToStart();
        for (int index = 0; index < 18; index++)
            _ = area.HandleKey(GuideXosTextInputKey.Right);
        ReadOnlySpan<byte> bytes = MixedDocument;
        return area.CaretIndex <= area.Length &&
            IsValidCaretBoundary(bytes, area.CaretIndex) &&
            area.ToUtf8().AsSpan().SequenceEqual(bytes) &&
            area.MaximumFirstVisibleLine == 0;
    }

    private static bool CrOnlyRejectedAtomically()
    {
        GuideXosTextArea area = new();
        if (!area.SetUtf8("keep\r\nthis"u8)) return false;
        byte[] before = area.ToUtf8();
        int caret = area.CaretIndex;
        int lines = area.LineCount;
        return !area.SetUtf8("bad\rline"u8) &&
            !area.SetUtf8("bad-at-eof\r"u8) &&
            area.ToUtf8().AsSpan().SequenceEqual(before) &&
            area.CaretIndex == caret && area.LineCount == lines;
    }

    private static bool SetTextSupportsCrLf()
    {
        GuideXosTextArea area = new();
        return area.SetText("one\r\ntwo") && area.LineCount == 2 &&
            area.Text == "one\r\ntwo" && area.Length == 8;
    }

    private static bool HorizontalMovementSkipsCrLfPair()
    {
        GuideXosTextArea area = new();
        if (!area.SetUtf8("abc\r\nd"u8)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (area.HandleKey(GuideXosTextInputKey.Left) !=
                GuideXosTextAreaEditResult.Ignored ||
            !MoveRight(area, 3) || area.CaretIndex != 3 ||
            area.CaretLine != 0 || area.CaretColumn != 3 ||
            area.HandleKey(GuideXosTextInputKey.Right) !=
                GuideXosTextAreaEditResult.Moved || area.CaretIndex != 5 ||
            area.CaretLine != 1 || area.CaretColumn != 0 ||
            area.HandleKey(GuideXosTextInputKey.Left) !=
                GuideXosTextAreaEditResult.Moved || area.CaretIndex != 3)
        {
            return false;
        }
        area.SetCaretToStart();
        for (int index = 0; index < area.Length; index++)
            _ = area.HandleKey(GuideXosTextInputKey.Right);
        return area.CaretIndex == area.Length &&
            area.HandleKey(GuideXosTextInputKey.Right) ==
                GuideXosTextAreaEditResult.Ignored;
    }

    private static bool VerticalPreferredColumn()
    {
        GuideXosTextArea area = new(256, 32, 4, 32);
        if (!area.SetUtf8(MixedDocument)) return false;
        area.Focus();
        area.SetCaretToStart();
        return MoveRight(area, 6) &&
            area.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosTextAreaEditResult.Moved && area.CaretLine == 1 &&
            area.CaretColumn == 1 && area.PreferredColumn == 6 &&
            area.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosTextAreaEditResult.Moved && area.CaretLine == 2 &&
            area.CaretColumn == 5 && area.PreferredColumn == 6 &&
            area.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosTextAreaEditResult.Moved && area.CaretLine == 3 &&
            area.CaretColumn == 6 && area.PreferredColumn == 6 &&
            area.HandleKey(GuideXosTextInputKey.Up) ==
                GuideXosTextAreaEditResult.Moved && area.CaretLine == 2 &&
            area.CaretColumn == 5 && area.PreferredColumn == 6;
    }

    private static bool HomeAndEndUseLogicalLineEdges()
    {
        GuideXosTextArea area = new();
        if (!area.SetUtf8("abc\r\ndef\r\ng"u8)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 2) ||
            area.HandleKey(GuideXosTextInputKey.End) !=
                GuideXosTextAreaEditResult.Moved || area.CaretIndex != 3 ||
            area.HandleKey(GuideXosTextInputKey.Home) !=
                GuideXosTextAreaEditResult.Moved || area.CaretIndex != 0 ||
            area.HandleKey(GuideXosTextInputKey.Down) !=
                GuideXosTextAreaEditResult.Moved || area.CaretLine != 1)
        {
            return false;
        }
        return area.HandleKey(GuideXosTextInputKey.End) ==
                GuideXosTextAreaEditResult.Moved && area.CaretIndex == 8 &&
            area.CaretLine == 1 && area.CaretColumn == 3;
    }

    private static bool SelectionCanCoverWholeDelimiter()
    {
        GuideXosTextArea area = new();
        if (!area.SetUtf8("a\r\nb"u8)) return false;
        area.Focus();
        area.SetCaretToStart();
        return area.HandleKey(GuideXosTextInputKey.Right) ==
                GuideXosTextAreaEditResult.Moved && area.CaretIndex == 1 &&
            area.HandleKey(GuideXosTextInputKey.Right, shift: true) ==
                GuideXosTextAreaEditResult.Moved && area.CaretIndex == 3 &&
            area.AnchorIndex == 1 && area.SelectedText == "\r\n" &&
            IsValidCaretBoundary("a\r\nb"u8, area.AnchorIndex) &&
            IsValidCaretBoundary("a\r\nb"u8, area.CaretIndex);
    }

    private static bool VerticalScrollingKeepsCaretVisible()
    {
        GuideXosTextArea area = new(256, 32, 4, 32);
        if (!area.SetUtf8("00\r\n01\n02\r\n03\n04\r\n05\n06\r\n07\n08\r\n09\n10\r\n11\n12\r\n13\n14\r\n15\n16\r\n17\n18\r\n19"u8))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        if (!MoveDown(area, 18) || area.FirstVisibleLine <= 0 ||
            area.FirstVisibleLine > area.MaximumFirstVisibleLine ||
            area.CaretLine < area.FirstVisibleLine ||
            area.CaretLine >= area.FirstVisibleLine + area.VisibleLineCount)
        {
            return false;
        }
        return MoveDown(area, 1) && area.CaretLine == 19 &&
            area.FirstVisibleLine <= area.MaximumFirstVisibleLine &&
            area.CaretLine < area.FirstVisibleLine + area.VisibleLineCount;
    }

    private static bool LongLinesRemainNonWrapping()
    {
        Span<byte> bytes = stackalloc byte[80];
        bytes.Fill((byte)'x');
        GuideXosTextArea area = new(256, 32, 4, 32);
        if (!area.SetUtf8(bytes)) return false;
        area.Focus();
        area.SetCaretToStart();
        for (int index = 0; index < 70; index++)
            _ = area.HandleKey(GuideXosTextInputKey.Right);
        return area.LineCount == 1 && area.CaretIndex == 70 &&
            area.CaretColumn == 70 && area.MaximumRenderableColumns == 32 &&
            area.FirstVisibleLine == 0;
    }

    private static bool BackspaceRemovesCrLfAtomically()
    {
        if (!CreateDocument("ab\r\ncd"u8, "/system/apps/C176/backspace.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 3) || area.CaretIndex != 4 ||
            area.HandleKey(GuideXosTextInputKey.Backspace) !=
                GuideXosTextAreaEditResult.Changed || area.Text != "abcd" ||
            area.CaretIndex != 2 || !document.Dirty)
        {
            return false;
        }
        return area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("ab\r\ncd"u8) &&
            !document.Dirty && area.Redo() ==
                GuideXosTextAreaEditResult.Changed && area.Text == "abcd" &&
            document.Dirty;
    }

    private static bool DeleteRemovesCrLfAtomically()
    {
        if (!CreateDocument("ab\r\ncd"u8, "/system/apps/C176/delete.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 2) || area.CaretIndex != 2 ||
            area.HandleKey(GuideXosTextInputKey.Delete) !=
                GuideXosTextAreaEditResult.Changed || area.Text != "abcd" ||
            area.CaretIndex != 2 || !document.Dirty)
        {
            return false;
        }
        return area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("ab\r\ncd"u8) &&
            !document.Dirty && area.Redo() ==
                GuideXosTextAreaEditResult.Changed && area.Text == "abcd" &&
            document.Dirty;
    }

    private static bool EnterAndHistoryKeepValidBoundaries()
    {
        if (!CreateDocument("ab\r\ncd"u8, "/system/apps/C176/enter.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 2) ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed ||
            !area.ToUtf8().AsSpan().SequenceEqual("ab\r\n\r\ncd"u8) ||
            !IsValidCaretBoundary("ab\r\n\r\ncd"u8, area.CaretIndex))
        {
            return false;
        }
        return document.Dirty && area.Undo() ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("ab\r\ncd"u8) &&
            !document.Dirty && area.Redo() ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("ab\r\n\r\ncd"u8) &&
            document.Dirty;
    }

    private static bool CopyAndPastePreserveCrLf()
    {
        GuideXosTextArea source = new();
        if (!source.SetUtf8("a\r\nb"u8)) return false;
        source.Focus();
        source.SetCaretToStart();
        if (!MoveRight(source, 1) ||
            source.HandleKey(GuideXosTextInputKey.Right, shift: true) !=
                GuideXosTextAreaEditResult.Moved)
        {
            return false;
        }
        Span<byte> copied = stackalloc byte[4];
        if (!source.TryCopySelectionTo(copied, out int copiedLength) ||
            !copied[..copiedLength].SequenceEqual("\r\n"u8))
        {
            return false;
        }

        GuideXosTextArea destination = new();
        if (!destination.SetUtf8("z"u8)) return false;
        destination.Focus();
        destination.SetCaretToStart();
        if (destination.PasteText(copied[..copiedLength]) !=
                GuideXosTextAreaEditResult.Changed ||
            !destination.ToUtf8().AsSpan().SequenceEqual("\r\nz"u8) ||
            destination.CaretIndex != 2 ||
            !IsValidCaretBoundary(destination.ToUtf8(), destination.CaretIndex))
        {
            return false;
        }
        byte[] before = destination.ToUtf8();
        return destination.PasteText("\r"u8) ==
                GuideXosTextAreaEditResult.Rejected &&
            destination.ToUtf8().AsSpan().SequenceEqual(before);
    }

    private static bool ResizePreservesEditorState()
    {
        if (!CreateDocument(MixedDocument, "/system/apps/C176/resize.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 2) || area.HandleKey(GuideXosTextInputKey.Down) !=
                GuideXosTextAreaEditResult.Moved ||
            area.HandleKey(GuideXosTextInputKey.Down, shift: true) !=
                GuideXosTextAreaEditResult.Moved || area.PreferredColumn != 2)
        {
            return false;
        }

        int caret = area.CaretIndex;
        int anchor = area.AnchorIndex;
        int preferred = area.PreferredColumn;
        int historyCount = area.HistoryCount;
        int historyIndex = area.HistoryIndex;
        string path = document.CurrentPath;
        Span<byte> before = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        if (!area.TryCopyUtf8To(before, out int beforeLength)) return false;
        int width = 600;
        int height = 360;
        if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 560, 344) ||
            !GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 960, 436))
        {
            return false;
        }
        Span<byte> after = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        return area.TryCopyUtf8To(after, out int afterLength) &&
            beforeLength == afterLength && before[..beforeLength].SequenceEqual(
                after[..afterLength]) && area.CaretIndex == caret &&
            area.AnchorIndex == anchor && area.PreferredColumn == preferred &&
            area.HistoryCount == historyCount && area.HistoryIndex == historyIndex &&
            !document.Dirty && document.CurrentPath == path &&
            area.IsFocused && area.CaretLine >= area.FirstVisibleLine &&
            area.CaretLine < area.FirstVisibleLine + area.VisibleLineCount &&
            area.FirstVisibleLine <= area.MaximumFirstVisibleLine &&
            IsValidCaretBoundary(after[..afterLength], area.CaretIndex) &&
            IsValidCaretBoundary(after[..afterLength], area.AnchorIndex);
    }

    private static bool MixedDocumentHydratesAndSerializesExactly()
    {
        ReadOnlySpan<byte> content =
            "left\r\nmiddle\nright\r\nlast"u8;
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState document = new();
        return document.TryHydrate(area, "/system/apps/C176/mixed.txt",
                content) &&
            document.CurrentPath == "/system/apps/C176/mixed.txt" &&
            area.LineCount == CountLines(content) &&
            area.ToUtf8().AsSpan().SequenceEqual(content) && !document.Dirty;
    }

    private static bool CreateDocument(ReadOnlySpan<byte> content, string path,
        out GuideXosTextArea area, out GuideXosNotesDocumentState document)
    {
        area = new GuideXosTextArea(256, 32, 4, 32);
        document = new GuideXosNotesDocumentState();
        return document.TryHydrate(area, path, content);
    }

    private static bool MoveRight(GuideXosTextArea area, int count)
    {
        for (int index = 0; index < count; index++)
        {
            if (area.HandleKey(GuideXosTextInputKey.Right) !=
                GuideXosTextAreaEditResult.Moved) return false;
        }
        return true;
    }

    private static bool MoveDown(GuideXosTextArea area, int count)
    {
        for (int index = 0; index < count; index++)
        {
            if (area.HandleKey(GuideXosTextInputKey.Down) !=
                GuideXosTextAreaEditResult.Moved) return false;
        }
        return true;
    }

    private static bool IsValidEditorState(GuideXosTextArea area,
        Span<byte> bytes, out int length)
    {
        if (!area.TryCopyUtf8To(bytes, out length) ||
            area.LineCount != CountLines(bytes[..length]) ||
            area.CaretIndex < 0 || area.CaretIndex > length ||
            area.AnchorIndex < 0 || area.AnchorIndex > length ||
            !IsValidCaretBoundary(bytes[..length], area.CaretIndex) ||
            !IsValidCaretBoundary(bytes[..length], area.AnchorIndex) ||
            area.CaretLine < 0 || area.CaretLine >= area.LineCount ||
            area.CaretColumn < 0 ||
            area.CaretColumn > GetLineLength(bytes[..length], area.CaretLine) ||
            area.FirstVisibleLine < 0 ||
            area.FirstVisibleLine > area.MaximumFirstVisibleLine ||
            area.CaretLine < area.FirstVisibleLine ||
            area.CaretLine >= area.FirstVisibleLine + area.VisibleLineCount)
        {
            return false;
        }
        return true;
    }

    private static bool IsValidCaretBoundary(ReadOnlySpan<byte> bytes,
        int index) => index >= 0 && index <= bytes.Length &&
        !(index > 0 && index < bytes.Length &&
            bytes[index - 1] == (byte)'\r' && bytes[index] == (byte)'\n');

    private static int CountLines(ReadOnlySpan<byte> bytes)
    {
        int lines = 1;
        for (int index = 0; index < bytes.Length; index++)
            if (bytes[index] == (byte)'\n') ++lines;
        return lines;
    }

    private static int GetLineLength(ReadOnlySpan<byte> bytes, int line)
    {
        int currentLine = 0;
        int start = 0;
        for (int index = 0; index < bytes.Length; index++)
        {
            if (bytes[index] != (byte)'\n') continue;
            if (currentLine == line)
            {
                int end = index;
                if (end > start && bytes[end - 1] == (byte)'\r') --end;
                return end - start;
            }
            ++currentLine;
            start = index + 1;
        }
        return currentLine == line ? bytes.Length - start : -1;
    }

    private static bool Case(ref int cases, bool condition)
    {
        ++cases;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = cases;
        return condition;
    }
}
#endif
