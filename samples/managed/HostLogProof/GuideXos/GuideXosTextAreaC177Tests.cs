#if HOSTLOGPROOF_C177_MANAGED_NOTES_NEWLINE_SAVE
using System;
using System.Text;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosTextAreaC177Tests
{
    public const int CaseCount = 23;
    public const int StressCaseCount = 8;
    public const int StressEditingNavigationOperations = 1000;
    public const int StressMixedInsertions = 250;
    public const int StressCapacityRejections = 250;
    public const int StressAlternatingResizes = 500;
    public const int StressRejectedGeometryRequests = 250;
    public const int StressLifecycleCycles = 100;
    public const int StressRoundTrips = 100;

    public static int FirstFailedCase { get; private set; }
    public static int EditingNavigationOperationCount { get; private set; }
    public static int MixedInsertionCount { get; private set; }
    public static int MixedRemovalCount { get; private set; }
    public static int CapacityRejectionCount { get; private set; }
    public static int AlternatingResizeCount { get; private set; }
    public static int RejectedGeometryCount { get; private set; }
    public static int LifecycleCount { get; private set; }
    public static int RoundTripCount { get; private set; }
    public static long ResizeAllocationBytes { get; private set; }
    public static int GeometryDriftCount { get; private set; }

    private static ReadOnlySpan<byte> MixedLfFirst => "a\nb\r\nc"u8;
    private static ReadOnlySpan<byte> MixedCrLfFirst => "a\r\nb\nc"u8;

    public static bool Run(GuideXosHost host)
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        passed &= Case(ref cases, EmptyDocumentUsesLf());
        passed &= Case(ref cases, LfDocumentUsesLf());
        passed &= Case(ref cases, CrLfDocumentUsesCrLf());
        passed &= Case(ref cases, DocumentWithoutNewlinesUsesLf());
        passed &= Case(ref cases, MixedPrefersNearestPrecedingDelimiter());
        passed &= Case(ref cases, MixedUsesFirstFollowingDelimiter());
        passed &= Case(ref cases, BeginningAndEndUseLocalDelimiter());
        passed &= Case(ref cases, EnterAdjacentToExistingBoundary());
        passed &= Case(ref cases, SelectionReplacementUsesLfWidth());
        passed &= Case(ref cases, SelectionReplacementUsesCrLfWidth());
        passed &= Case(ref cases, ExactLfCapacitySucceeds());
        passed &= Case(ref cases, ExactCrLfCapacitySucceeds());
        passed &= Case(ref cases, CrLfCapacityFailureIsAtomic());
        passed &= Case(ref cases, LogicalLineCapacityFailureIsAtomic());
        passed &= Case(ref cases, UndoRedoRestoresLfEnter());
        passed &= Case(ref cases, UndoRedoRestoresCrLfEnter());
        passed &= Case(ref cases, DirtyTracksSuccessfulEnterAndHistory());
        passed &= Case(ref cases, FailedEditKeepsDocumentState());
        passed &= Case(ref cases, ExistingBytesRoundTripExactly());
        passed &= Case(ref cases, CrOnlyRemainsUnsupportedAtomically());
        passed &= Case(ref cases, MaximumEditorCapacityCountsSerializedBytes());
        passed &= Case(ref cases, ResizeThenEnterPreservesState());
        passed &= Case(ref cases, host == null || GuestFileSaveAndReopen(host));

        if (cases != CaseCount) passed = false;
        host?.TryLog(passed
            ? "C177-NOTES-NEWLINE focused=23 result=PASS"u8
            : "C177-NOTES-NEWLINE result=FAIL"u8);
        host?.TryLog(FirstFailedCase == 0
            ? "C177-NOTES-NEWLINE-DETAIL firstFailedCase=0"u8
            : "C177-NOTES-NEWLINE-DETAIL firstFailedCase=nonzero"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        EditingNavigationOperationCount = 0;
        MixedInsertionCount = 0;
        MixedRemovalCount = 0;
        CapacityRejectionCount = 0;
        AlternatingResizeCount = 0;
        RejectedGeometryCount = 0;
        LifecycleCount = 0;
        RoundTripCount = 0;
        ResizeAllocationBytes = -1;
        GeometryDriftCount = 0;

        bool operations = RunEditingNavigationStress();
        bool mixed = RunMixedDelimiterStress();
        bool capacity = RunCapacityStress();
        bool resize = RunResizeStress();
        bool lifecycle = RunLifecycleStress();
        bool roundTrips = RunSerializationStress();

        bool passed = operations && mixed && capacity && resize && lifecycle &&
            roundTrips && EditingNavigationOperationCount ==
                StressEditingNavigationOperations &&
            MixedInsertionCount == StressMixedInsertions &&
            MixedRemovalCount == StressMixedInsertions &&
            CapacityRejectionCount == StressCapacityRejections &&
            AlternatingResizeCount == StressAlternatingResizes &&
            RejectedGeometryCount == StressRejectedGeometryRequests &&
            LifecycleCount == StressLifecycleCycles &&
            RoundTripCount == StressRoundTrips && ResizeAllocationBytes == 0 &&
            GeometryDriftCount == 0;
        host?.TryLog(passed
            ? "C177-STRESS operations=1000 mixedInsertions=250 mixedRemovals=250 capacityRejected=250 alternatingResizes=500 rejectedGeometry=250 lifecycles=100 roundTrips=100 resizeAllocationBytes=0 geometryDrift=0 result=PASS"u8
            : "C177-STRESS result=FAIL"u8);
        return passed;
    }

    private static bool EmptyDocumentUsesLf()
    {
        GuideXosTextArea area = new();
        GuideXosNotesDocumentState document = new();
        if (!document.InitializeUntitled(area)) return false;
        area.Focus();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\n"u8) &&
            area.CaretIndex == 1 && area.LineCount == 2 && document.Dirty;
    }

    private static bool LfDocumentUsesLf()
    {
        GuideXosTextArea area = NewArea("one\ntwo\nthree"u8);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 7) ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual("one\ntwo\n\nthree"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool CrLfDocumentUsesCrLf()
    {
        GuideXosTextArea area = NewArea("one\r\ntwo\r\nthree"u8);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 8) ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual(
                "one\r\ntwo\r\n\r\nthree"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool DocumentWithoutNewlinesUsesLf()
    {
        GuideXosTextArea area = NewArea("abcd"u8);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 2) ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual("ab\ncd"u8);
    }

    private static bool MixedPrefersNearestPrecedingDelimiter()
    {
        GuideXosTextArea area = NewArea(MixedCrLfFirst);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 4) ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual("a\r\nb\r\n\nc"u8);
    }

    private static bool MixedUsesFirstFollowingDelimiter()
    {
        GuideXosTextArea area = NewArea(MixedLfFirst);
        area.Focus();
        area.SetCaretToStart();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\na\nb\r\nc"u8);
    }

    private static bool BeginningAndEndUseLocalDelimiter()
    {
        GuideXosTextArea beginning = NewArea("a\r\nb"u8);
        beginning.Focus();
        beginning.SetCaretToStart();
        if (beginning.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed ||
            !beginning.ToUtf8().AsSpan().SequenceEqual("\r\na\r\nb"u8))
        {
            return false;
        }

        GuideXosTextArea end = NewArea("a\r\nb"u8);
        end.Focus();
        return end.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            end.ToUtf8().AsSpan().SequenceEqual("a\r\nb\r\n"u8) &&
            IsValidBoundary(end.ToUtf8(), end.CaretIndex);
    }

    private static bool EnterAdjacentToExistingBoundary()
    {
        GuideXosTextArea area = NewArea("a\r\nb"u8);
        area.Focus();
        return MoveToPhysicalIndex(area, 1) &&
            area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("a\r\n\r\nb"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool SelectionReplacementUsesLfWidth()
    {
        GuideXosTextArea area = NewArea("a\nbc\nd"u8);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 2) ||
            area.HandleKey(GuideXosTextInputKey.Right, shift: true) !=
                GuideXosTextAreaEditResult.Moved ||
            area.HandleKey(GuideXosTextInputKey.Right, shift: true) !=
                GuideXosTextAreaEditResult.Moved ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual("a\n\n\nd"u8) &&
            area.Length == 5 && area.CaretIndex == 3 &&
            area.LineCount == 4 && IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool SelectionReplacementUsesCrLfWidth()
    {
        GuideXosTextArea area = NewArea("a\r\nbc"u8);
        area.Focus();
        if (!MoveToPhysicalIndex(area, 3) ||
            area.HandleKey(GuideXosTextInputKey.Right, shift: true) !=
                GuideXosTextAreaEditResult.Moved ||
            area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed)
        {
            return false;
        }
        return area.ToUtf8().AsSpan().SequenceEqual("a\r\n\r\nc"u8) &&
            area.Length == 6 && area.CaretIndex == 5 && area.LineCount == 3 &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool ExactLfCapacitySucceeds()
    {
        GuideXosTextArea area = new(2, 8, 4, 8);
        if (!area.SetUtf8("x"u8)) return false;
        area.Focus();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && area.Length == 2 &&
            area.ToUtf8().AsSpan().SequenceEqual("x\n"u8);
    }

    private static bool ExactCrLfCapacitySucceeds()
    {
        GuideXosTextArea area = new(6, 8, 4, 8);
        if (!area.SetUtf8("a\r\nb"u8)) return false;
        area.Focus();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && area.Length == 6 &&
            area.ToUtf8().AsSpan().SequenceEqual("a\r\nb\r\n"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool CrLfCapacityFailureIsAtomic()
    {
        GuideXosTextArea area = new(5, 8, 4, 8);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C177/capacity.txt",
                "a\r\nb"u8)) return false;
        area.Focus();
        byte[] before = area.ToUtf8();
        int history = area.HistoryCount;
        GuideXosTextRevision revision = area.CurrentRevision;
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Rejected &&
            area.ToUtf8().AsSpan().SequenceEqual(before) && area.CaretIndex == 4 &&
            area.AnchorIndex == 4 && area.LineCount == 2 &&
            area.HistoryCount == history && area.CurrentRevision.Equals(revision) &&
            !document.Dirty;
    }

    private static bool LogicalLineCapacityFailureIsAtomic()
    {
        GuideXosTextArea area = new(16, 2, 4, 8);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C177/lines.txt", "a\nb"u8))
            return false;
        area.Focus();
        byte[] before = area.ToUtf8();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Rejected &&
            area.ToUtf8().AsSpan().SequenceEqual(before) && area.LineCount == 2 &&
            !document.Dirty;
    }

    private static bool UndoRedoRestoresLfEnter()
    {
        GuideXosTextArea area = NewArea("a\nb"u8);
        area.Focus();
        area.SetCaretToStart();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\na\nb"u8) &&
            area.Undo() == GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("a\nb"u8) &&
            area.Redo() == GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\na\nb"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool UndoRedoRestoresCrLfEnter()
    {
        GuideXosTextArea area = NewArea("a\r\nb"u8);
        area.Focus();
        area.SetCaretToStart();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\r\na\r\nb"u8) &&
            area.CaretIndex == 2 && area.Undo() ==
                GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("a\r\nb"u8) &&
            area.Redo() == GuideXosTextAreaEditResult.Changed &&
            area.ToUtf8().AsSpan().SequenceEqual("\r\na\r\nb"u8) &&
            area.CaretIndex == 2 && IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool DirtyTracksSuccessfulEnterAndHistory()
    {
        GuideXosTextArea area = new();
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C177/dirty.txt", "a\r\nb"u8))
            return false;
        area.Focus();
        area.SetCaretToStart();
        if (document.Dirty || area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed || !document.Dirty)
        {
            return false;
        }
        return area.Undo() == GuideXosTextAreaEditResult.Changed &&
            !document.Dirty && area.Redo() == GuideXosTextAreaEditResult.Changed &&
            document.Dirty;
    }

    private static bool FailedEditKeepsDocumentState()
    {
        GuideXosTextArea area = new(4, 8, 4, 8);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C177/fail.txt", "a\r\nb"u8))
            return false;
        area.Focus();
        byte[] before = area.ToUtf8();
        GuideXosTextRevision revision = area.CurrentRevision;
        int history = area.HistoryCount;
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Rejected &&
            area.ToUtf8().AsSpan().SequenceEqual(before) &&
            area.CurrentRevision.Equals(revision) && area.HistoryCount == history &&
            area.CaretIndex == 4 && area.LineCount == 2 && !document.Dirty;
    }

    private static bool ExistingBytesRoundTripExactly()
    {
        ReadOnlySpan<byte> lf = "a\nb"u8;
        ReadOnlySpan<byte> crlf = "a\r\nb"u8;
        ReadOnlySpan<byte> mixed = "a\r\nb\nc\r\n"u8;
        ReadOnlySpan<byte> trailingLf = "tail\n"u8;
        ReadOnlySpan<byte> noTrailingNewline = "tail"u8;
        ReadOnlySpan<byte> blankLines = "\r\n\n\r\n"u8;
        return RoundTrip(lf) && RoundTrip(crlf) && RoundTrip(mixed) &&
            RoundTrip(trailingLf) && RoundTrip(noTrailingNewline) &&
            RoundTrip(blankLines);
    }

    private static bool CrOnlyRemainsUnsupportedAtomically()
    {
        GuideXosTextArea area = NewArea("keep\r\nthis"u8);
        byte[] before = area.ToUtf8();
        int caret = area.CaretIndex;
        int lines = area.LineCount;
        return !area.SetUtf8("unsupported\rformat"u8) &&
            area.ToUtf8().AsSpan().SequenceEqual(before) &&
            area.CaretIndex == caret && area.LineCount == lines;
    }

    private static bool MaximumEditorCapacityCountsSerializedBytes()
    {
        GuideXosTextArea area = new(
            GuideXosTextArea.MaximumSupportedCharacters,
            GuideXosTextArea.MaximumSupportedLines,
            8,
            GuideXosTextArea.MaximumSupportedRenderableColumns);
        byte[] content = new byte[GuideXosTextArea.MaximumSupportedCharacters - 1];
        content.AsSpan().Fill((byte)'x');
        content[^1] = (byte)'\n';
        if (!area.SetUtf8(content)) return false;
        area.Focus();
        int previousLength = area.Length;
        area.SetCaretToStart();
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && area.Length ==
                GuideXosTextArea.MaximumSupportedCharacters &&
            previousLength == GuideXosTextArea.MaximumSupportedCharacters - 1 &&
            area.ToUtf8()[0] == (byte)'\n';
    }

    private static bool ResizeThenEnterPreservesState()
    {
        if (!CreateDocument("a\r\nb\nc"u8, "/system/apps/C177/resize.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        area.SetCaretToStart();
        if (!MoveRight(area, 3)) return false;
        int caret = area.CaretIndex;
        int anchor = area.AnchorIndex;
        int width = 600;
        int height = 360;
        if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 560, 344) ||
            !GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 960, 436) ||
            area.CaretIndex != caret || area.AnchorIndex != anchor || document.Dirty)
        {
            return false;
        }
        return area.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosTextAreaEditResult.Changed && document.Dirty &&
            area.ToUtf8().AsSpan().SequenceEqual("a\r\nb\r\n\nc"u8) &&
            IsValidBoundary(area.ToUtf8(), area.CaretIndex);
    }

    private static bool GuestFileSaveAndReopen(GuideXosHost host)
    {
        const string path = "/system/apps/C152/c177.txt";
        const string invalidPath = "/system/apps/C152/../c177bad.txt";
        ReadOnlySpan<byte> original = "left\r\nmiddle\nright\r\nlast"u8;
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        if (GuideXosFile.TryGetInfo(host, pathBytes, out _) !=
                GuideXosFileResult.NotFound ||
            GuideXosFile.WriteAllTextUtf8(host, pathBytes, original) !=
                GuideXosFileResult.Success ||
            !ReadEquals(host, pathBytes, original))
        {
            return false;
        }

        GuideXosTextArea area = new(GuideXosTextArea.DefaultMaximumCharacters,
            32, 4, 48);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, path, original) || document.Dirty ||
            !area.ToUtf8().AsSpan().SequenceEqual(original) ||
            GuideXosFile.WriteAllTextUtf8(host, pathBytes, area.ToUtf8()) !=
                GuideXosFileResult.Success || !ReadEquals(host, pathBytes, original))
        {
            return false;
        }

        area.Focus();
        area.SetCaretToStart();
        if (area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed || !document.Dirty ||
            !ReadEquals(host, pathBytes, original))
        {
            return false;
        }
        ReadOnlySpan<byte> modified = "\r\nleft\r\nmiddle\nright\r\nlast"u8;
        byte[] snapshot = area.ToUtf8();
        int history = area.HistoryCount;
        if (!snapshot.AsSpan().SequenceEqual(modified) ||
            GuideXosFile.WriteAllTextUtf8(host, pathBytes, snapshot) !=
                GuideXosFileResult.Success || !ReadEquals(host, pathBytes, modified) ||
            !document.MarkSaveSucceeded(path) || document.Dirty ||
            area.HistoryCount != history)
        {
            return false;
        }

        GuideXosTextArea reopened = new(GuideXosTextArea.DefaultMaximumCharacters,
            32, 4, 48);
        GuideXosNotesDocumentState reopenedState = new();
        if (!reopenedState.TryHydrate(reopened, path, modified) ||
            reopenedState.Dirty || reopenedState.CurrentPath != path ||
            !reopened.ToUtf8().AsSpan().SequenceEqual(modified))
        {
            return false;
        }

        if (!MoveToPhysicalIndex(area, 3)) return false;
        if (area.HandleCharacter('!') != GuideXosTextAreaEditResult.Changed)
            return false;
        byte[] failedSnapshot = area.ToUtf8();
        int failedHistory = area.HistoryCount;
        GuideXosFileResult invalid = GuideXosFile.WriteAllTextUtf8(host,
            Encoding.UTF8.GetBytes(invalidPath), failedSnapshot);
        document.MarkSaveFailed();
        if (invalid != GuideXosFileResult.InvalidPath || !document.Dirty ||
            document.CurrentPath != path || area.HistoryCount != failedHistory ||
            !area.ToUtf8().AsSpan().SequenceEqual(failedSnapshot) ||
            !ReadEquals(host, pathBytes, modified))
        {
            return false;
        }

        const string tooLargePath = "/system/apps/C152/c177big.txt";
        byte[] tooLargePathBytes = Encoding.UTF8.GetBytes(tooLargePath);
        Span<byte> overLimit = stackalloc byte[
            GuideXosTextArea.DefaultMaximumCharacters + 1];
        overLimit.Fill((byte)'x');
        if (GuideXosFile.TryGetInfo(host, tooLargePathBytes, out _) !=
                GuideXosFileResult.NotFound ||
            GuideXosFile.WriteAllTextUtf8(host, tooLargePathBytes, overLimit) !=
                GuideXosFileResult.FileTooLarge ||
            GuideXosFile.TryGetInfo(host, tooLargePathBytes, out _) !=
                GuideXosFileResult.NotFound)
        {
            return false;
        }
        host.TryLog("C177-SAVE-PROOF unchanged=exact modified=exact reopen=exact failure=preserved invalid=explicit capacity=no-file result=PASS"u8);
        return true;
    }

    private static bool ReadEquals(GuideXosHost host, ReadOnlySpan<byte> path,
        ReadOnlySpan<byte> expected)
    {
        Span<byte> actual = stackalloc byte[
            GuideXosTextArea.DefaultMaximumCharacters];
        GuideXosFileResult result = GuideXosFile.ReadAllTextUtf8(host, path,
            actual, out int read);
        return result == GuideXosFileResult.Success && read == expected.Length &&
            actual[..read].SequenceEqual(expected);
    }

    private static bool RunEditingNavigationStress()
    {
        GuideXosTextArea area = NewArea("base"u8);
        area.Focus();
        Span<byte> observed = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        bool passed = true;
        for (int cycle = 0; cycle < 250; cycle++)
        {
            area.SetCaretToStart();
            GuideXosTextAreaEditResult insert = area.HandleCharacter('x');
            ++EditingNavigationOperationCount;
            if (insert != GuideXosTextAreaEditResult.Changed ||
                !area.TryCopyUtf8To(observed, out int insertedLength) ||
                !observed[..insertedLength].SequenceEqual("xbase"u8))
            {
                passed = false;
            }

            GuideXosTextAreaEditResult backspace = area.HandleKey(
                GuideXosTextInputKey.Backspace);
            ++EditingNavigationOperationCount;
            if (backspace != GuideXosTextAreaEditResult.Changed ||
                !area.TryCopyUtf8To(observed, out int removedLength) ||
                !observed[..removedLength].SequenceEqual("base"u8))
            {
                passed = false;
            }

            area.SetCaretToStart();
            GuideXosTextAreaEditResult right = area.HandleKey(
                GuideXosTextInputKey.Right);
            ++EditingNavigationOperationCount;
            GuideXosTextAreaEditResult left = area.HandleKey(
                GuideXosTextInputKey.Left);
            ++EditingNavigationOperationCount;
            if (right != GuideXosTextAreaEditResult.Moved ||
                left != GuideXosTextAreaEditResult.Moved ||
                !area.TryCopyUtf8To(observed, out int stableLength) ||
                !observed[..stableLength].SequenceEqual("base"u8) ||
                !IsEditorBoundaryValid(area))
            {
                passed = false;
            }
        }

        return passed && EditingNavigationOperationCount ==
            StressEditingNavigationOperations;
    }

    private static bool RunMixedDelimiterStress()
    {
        GuideXosTextArea lfArea = NewArea(MixedLfFirst);
        GuideXosTextArea crlfArea = NewArea(MixedCrLfFirst);
        lfArea.Focus();
        crlfArea.Focus();
        bool passed = true;
        for (int cycle = 0; cycle < StressMixedInsertions / 2; cycle++)
        {
            lfArea.SetCaretToStart();
            if (!EnterAndRemove(lfArea, "\na\nb\r\nc"u8, MixedLfFirst))
                passed = false;

            crlfArea.SetCaretToStart();
            if (!MoveRight(crlfArea, 3) || crlfArea.CaretIndex != 4 ||
                crlfArea.HandleKey(GuideXosTextInputKey.Enter) !=
                    GuideXosTextAreaEditResult.Changed ||
                !crlfArea.ToUtf8().AsSpan().SequenceEqual("a\r\nb\r\n\nc"u8))
            {
                passed = false;
            }
            else
            {
                ++MixedInsertionCount;
            }
            if (crlfArea.HandleKey(GuideXosTextInputKey.Backspace) !=
                    GuideXosTextAreaEditResult.Changed ||
                !crlfArea.ToUtf8().AsSpan().SequenceEqual(MixedCrLfFirst))
            {
                passed = false;
            }
            else
            {
                ++MixedRemovalCount;
            }
        }
        return passed;
    }

    private static bool EnterAndRemove(GuideXosTextArea area,
        ReadOnlySpan<byte> withNewline, ReadOnlySpan<byte> original)
    {
        if (area.HandleKey(GuideXosTextInputKey.Enter) !=
                GuideXosTextAreaEditResult.Changed ||
            !area.ToUtf8().AsSpan().SequenceEqual(withNewline))
        {
            return false;
        }
        ++MixedInsertionCount;
        if (area.HandleKey(GuideXosTextInputKey.Backspace) !=
                GuideXosTextAreaEditResult.Changed ||
            !area.ToUtf8().AsSpan().SequenceEqual(original))
        {
            return false;
        }
        ++MixedRemovalCount;
        return true;
    }

    private static bool RunCapacityStress()
    {
        GuideXosTextArea area = new(5, 8, 4, 8);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C177/reject.txt", "a\r\nb"u8))
            return false;
        area.Focus();
        byte[] original = area.ToUtf8();
        GuideXosTextRevision revision = area.CurrentRevision;
        int historyCount = area.HistoryCount;
        for (int index = 0; index < StressCapacityRejections; index++)
        {
            if (area.HandleKey(GuideXosTextInputKey.Enter) !=
                    GuideXosTextAreaEditResult.Rejected ||
                !area.ToUtf8().AsSpan().SequenceEqual(original) ||
                area.CaretIndex != original.Length || area.LineCount != 2 ||
                area.HistoryCount != historyCount ||
                !area.CurrentRevision.Equals(revision) || document.Dirty)
            {
                return false;
            }
            ++CapacityRejectionCount;
        }
        return CapacityRejectionCount == StressCapacityRejections;
    }

    private static bool RunResizeStress()
    {
        if (!CreateDocument("line0\r\nline1\nline2\r\nline3"u8,
                "/system/apps/C177/resize-stress.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document)) return false;
        area.Focus();
        int width = 600;
        int height = 360;
        if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 560, 344) ||
            !GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                document, ref width, ref height, 960, 436))
        {
            return false;
        }

        long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        Span<byte> observed = stackalloc byte[
            GuideXosTextArea.MaximumSupportedCharacters];
        for (int index = 0; index < StressAlternatingResizes; index++)
        {
            bool minimum = (index & 1) == 0;
            int nextWidth = minimum ? 560 : 960;
            int nextHeight = minimum ? 344 : 436;
            if (!GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                    document, ref width, ref height, nextWidth, nextHeight) ||
                width != nextWidth || height != nextHeight || document.Dirty ||
                !IsEditorBoundaryValid(area, observed))
            {
                ++GeometryDriftCount;
            }
            ++AlternatingResizeCount;
        }

        for (int index = 0; index < StressRejectedGeometryRequests; index++)
        {
            int priorWidth = width;
            int priorHeight = height;
            if (GuideXosManagedWindowResizeC175Tests.TryResizeForC176(area,
                    document, ref width, ref height, 559, priorHeight) ||
                width != priorWidth || height != priorHeight || document.Dirty ||
                !IsEditorBoundaryValid(area, observed))
            {
                ++GeometryDriftCount;
            }
            ++RejectedGeometryCount;
        }
        ResizeAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocatedBefore;
        return AlternatingResizeCount == StressAlternatingResizes &&
            RejectedGeometryCount == StressRejectedGeometryRequests &&
            width == 960 && height == 436 && ResizeAllocationBytes == 0 &&
            GeometryDriftCount == 0;
    }

    private static bool RunLifecycleStress()
    {
        for (int cycle = 0; cycle < StressLifecycleCycles; cycle++)
        {
            ManagedNotes notes = new();
            ulong identity = (ulong)(0xC1770000u + (uint)cycle + 1u);
            if (!notes.BeginC175LifecycleForTests(identity) ||
                notes.C175WindowForTests != identity ||
                !notes.ResizeC175ForTests(960, 436) ||
                !notes.ResizeC175ForTests(560, 344) ||
                notes.C175WindowForTests != identity ||
                !notes.CloseC175LifecycleForTests() ||
                notes.C175WindowForTests != 0u)
            {
                return false;
            }
            ++LifecycleCount;
        }
        return LifecycleCount == StressLifecycleCycles;
    }

    private static bool RunSerializationStress()
    {
        ReadOnlySpan<byte> fixture = "a\r\nb\n\r\nlast\r\n"u8;
        Span<byte> serialized = stackalloc byte[
            GuideXosTextArea.MaximumSupportedCharacters];
        for (int cycle = 0; cycle < StressRoundTrips; cycle++)
        {
            GuideXosTextArea area = NewArea(fixture);
            if (!area.TryCopyUtf8To(serialized, out int length) ||
                !serialized[..length].SequenceEqual(fixture) ||
                !RoundTrip(fixture))
            {
                return false;
            }
            ++RoundTripCount;
        }
        return RoundTripCount == StressRoundTrips;
    }

    private static bool RoundTrip(ReadOnlySpan<byte> content)
    {
        GuideXosTextArea area = new();
        return area.SetUtf8(content) && area.ToUtf8().AsSpan().SequenceEqual(content);
    }

    private static bool CreateDocument(ReadOnlySpan<byte> content, string path,
        out GuideXosTextArea area, out GuideXosNotesDocumentState document)
    {
        area = new GuideXosTextArea(256, 32, 4, 32);
        document = new GuideXosNotesDocumentState();
        return document.TryHydrate(area, path, content);
    }

    private static GuideXosTextArea NewArea(ReadOnlySpan<byte> content)
    {
        GuideXosTextArea area = new();
        _ = area.SetUtf8(content);
        return area;
    }

    private static bool MoveToPhysicalIndex(GuideXosTextArea area, int target)
    {
        area.SetCaretToStart();
        while (area.CaretIndex < target)
        {
            if (area.HandleKey(GuideXosTextInputKey.Right) !=
                GuideXosTextAreaEditResult.Moved) return false;
        }
        return area.CaretIndex == target;
    }

    private static bool MoveToLineEnd(GuideXosTextArea area)
    {
        while (area.CaretIndex < area.Length)
        {
            GuideXosTextAreaEditResult result = area.HandleKey(
                GuideXosTextInputKey.Right);
            if (result != GuideXosTextAreaEditResult.Moved) return false;
        }
        return true;
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

    private static bool IsEditorBoundaryValid(GuideXosTextArea area)
    {
        byte[] text = area.ToUtf8();
        return IsEditorBoundaryValid(area, text);
    }

    private static bool IsEditorBoundaryValid(GuideXosTextArea area,
        Span<byte> text)
    {
        if (!area.TryCopyUtf8To(text, out int length)) return false;
        return IsEditorBoundaryValid(area, (ReadOnlySpan<byte>)text[..length]);
    }

    private static bool IsEditorBoundaryValid(GuideXosTextArea area,
        ReadOnlySpan<byte> text) =>
        IsValidBoundary(text, area.CaretIndex) &&
            IsValidBoundary(text, area.AnchorIndex) &&
            area.LineCount == CountLines(text);

    private static bool IsValidBoundary(ReadOnlySpan<byte> text, int index) =>
        index >= 0 && index <= text.Length &&
        !(index > 0 && index < text.Length &&
            text[index - 1] == (byte)'\r' && text[index] == (byte)'\n');

    private static int CountLines(ReadOnlySpan<byte> text)
    {
        int count = 1;
        for (int index = 0; index < text.Length; index++)
            if (text[index] == (byte)'\n') ++count;
        return count;
    }

    private static bool Case(ref int cases, bool condition)
    {
        ++cases;
        if (!condition && FirstFailedCase == 0) FirstFailedCase = cases;
        return condition;
    }
}
#endif
