#if HOSTLOGPROOF_C175_MANAGED_NOTES_RESIZE
using System;
using HostLogProof.Applications;

namespace HostLogProof;

public static class GuideXosManagedWindowResizeC175Tests
{
    public const int CaseCount = 24;
    public const int StressCaseCount = 7;
    public const int StressLayoutUpdates = 1000;
    public const int StressAlternatingResizes = 500;
    public const int StressFailedGeometryRequests = 250;
    public const int StressLifecycleCycles = 100;

    public static long StressAllocationBytes { get; private set; }
    public static int StressDriftCount { get; private set; }
    public static int StressRollbackCount { get; private set; }
    public static int StressLifecycleCount { get; private set; }
    public static int FirstFailedCase { get; private set; }

    public static bool Run(GuideXosHost host)
    {
        FirstFailedCase = 0;
        int cases = 0;
        bool passed = true;

        bool initialValid = TryLayout(600, 360,
            out GuideXosManagedNotesLayoutC175 initial);
        passed &= Case(ref cases, initialValid &&
            initial.ClientWidth == 600 && initial.ClientHeight == 336 &&
            initial.EditorColumns == 32 && initial.EditorLines == 4 &&
            initial.StatusY == 162 && initial.SettingsY == 292 &&
            initial.PathDisplayHeight == 72);

        bool minimumValid = TryLayout(560, 344,
            out GuideXosManagedNotesLayoutC175 minimum);
        passed &= Case(ref cases, minimumValid && minimum.ClientWidth == 560 &&
            minimum.ClientHeight == 320 && minimum.EditorColumns == 27 &&
            minimum.EditorLines == 4 && minimum.SettingsY + 28 <= 320 &&
            GuideXosManagedNotesLayoutC175.EditorY >=
                GuideXosManagedNotesLayoutC175.PathLabelY +
                    GuideXosManagedNotesLayoutC175.EditorLineHeight &&
            minimum.PathDisplayHeight == 54);

        bool maximumValid = TryLayout(960, 436,
            out GuideXosManagedNotesLayoutC175 maximum);
        passed &= Case(ref cases, maximumValid && maximum.ClientWidth == 960 &&
            maximum.ClientHeight == 412 && maximum.EditorColumns == 56 &&
            maximum.EditorLines == 8 && maximum.SettingsY + 28 <= 412 &&
            maximum.PathDisplayHeight == 72);

        passed &= Case(ref cases, !TryLayout(559, 344, out _));
        passed &= Case(ref cases, !TryLayout(560, 343, out _));
        passed &= Case(ref cases, !TryLayout(961, 436, out _));
        passed &= Case(ref cases, !TryLayout(960, 437, out _));

        GuideXosManagedWindowResizeTransactionC174 invalidWidth = new(
            in ManagedNotes.ResizeBoundsC175, 600, 360, 559, 344);
        passed &= Case(ref cases, !invalidWidth.IsValid &&
            !invalidWidth.RequiresRollback &&
            invalidWidth.Result == GuideXosResult.InvalidArgument);
        GuideXosManagedWindowResizeTransactionC174 invalidHeight = new(
            in ManagedNotes.ResizeBoundsC175, 600, 360, 560, 343);
        passed &= Case(ref cases, !invalidHeight.IsValid &&
            !invalidHeight.RequiresRollback);

        passed &= Case(ref cases, RepeatedGeometryRoundTrip());
        passed &= Case(ref cases, DocumentStability(ReadOnlySpan<byte>.Empty,
            "/system/apps/C175/empty.txt"));
        passed &= Case(ref cases, DocumentStability("hello from C164"u8,
            "/system/apps/C164/hello.txt"));
        passed &= Case(ref cases, DocumentStability(MultilineDocument,
            "/system/apps/C175/multiline.txt"));
        passed &= Case(ref cases, LongViewportDocumentStability());
        passed &= Case(ref cases, EditingStatePreservation());
        passed &= Case(ref cases, PointerCoordinatesAfterResize());
        passed &= Case(ref cases, EditorPointerFocusThenCharacterInsertion());
        passed &= Case(ref cases, ControlHostPointerFocusThenCharacterInsertion());
        passed &= Case(ref cases, KeyboardNavigationAfterResize());
        passed &= Case(ref cases, EditUndoRedoAfterResize());
        passed &= Case(ref cases, DirtyStateAndPathPreservation());
        passed &= Case(ref cases, ScrollPositionAdjustment());
        passed &= Case(ref cases, ExactRollbackAfterRejectedTransaction());
        passed &= Case(ref cases, RepeatedFailedGeometryRequests());

        if (cases != CaseCount) passed = false;
        host?.TryLog(passed
            ? "C175-NOTES-RESIZE-HOST focused=24 result=PASS"u8
            : "C175-NOTES-RESIZE-HOST result=FAIL"u8);
        return passed;
    }

    public static bool RunStress(GuideXosHost host)
    {
        StressAllocationBytes = -1;
        StressDriftCount = 0;
        StressRollbackCount = 0;
        StressLifecycleCount = 0;

        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState document = new();
        if (!document.TryHydrate(area, "/system/apps/C175/stress.txt",
                MultilineDocument))
        {
            return false;
        }
        int width = 600;
        int height = 360;
        for (int warmup = 0; warmup < 32; warmup++)
        {
            int targetWidth = (warmup & 1) == 0 ? 560 : 960;
            int targetHeight = (warmup & 1) == 0 ? 344 : 436;
            if (!TryResize(area, document, ref width, ref height,
                    targetWidth, targetHeight))
            {
                return false;
            }
        }

        long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < StressLayoutUpdates; index++)
        {
            bool toMinimum = (index & 1) == 0;
            int targetWidth = toMinimum ? 560 : 960;
            int targetHeight = toMinimum ? 344 : 436;
            if (!TryResize(area, document, ref width, ref height,
                    targetWidth, targetHeight))
            {
                ++StressDriftCount;
                continue;
            }
            if (width != targetWidth || height != targetHeight)
                ++StressDriftCount;
        }
        StressAllocationBytes = GC.GetAllocatedBytesForCurrentThread() -
            allocatedBefore;

        long rejectedResizeAllocationStart =
            GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < StressFailedGeometryRequests; index++)
        {
            int priorWidth = width;
            int priorHeight = height;
            GuideXosManagedWindowResizeTransactionC174 rejected = new(
                in ManagedNotes.ResizeBoundsC175, width, height,
                (index & 1) == 0 ? 559 : 961,
                (index & 1) == 0 ? height : height);
            if (rejected.IsValid || rejected.RequiresRollback ||
                rejected.PriorOuterWidth != priorWidth ||
                rejected.PriorOuterHeight != priorHeight)
            {
                ++StressDriftCount;
            }
            else
            {
                int rollbackWidth = priorWidth == 560 ? 960 : 560;
                int rollbackHeight = priorHeight == 344 ? 436 : 344;
                if (TryRejectedResizeWithRollback(area, document,
                        priorWidth, priorHeight, rollbackWidth,
                        rollbackHeight))
                {
                    ++StressRollbackCount;
                }
                else
                {
                    ++StressDriftCount;
                }
            }
        }
        StressAllocationBytes += GC.GetAllocatedBytesForCurrentThread() -
            rejectedResizeAllocationStart;

        for (int cycle = 0; cycle < StressLifecycleCycles; cycle++)
        {
            ManagedNotes notes = new();
            ulong identity = (ulong)(0xC1750000u + (uint)cycle + 1u);
            if (!notes.BeginC175LifecycleForTests(identity) ||
                notes.C175WindowForTests != identity ||
                !notes.ResizeC175ForTests(960, 436) ||
                !notes.ResizeC175ForTests(560, 344) ||
                notes.C175WindowForTests != identity ||
                !notes.CloseC175LifecycleForTests() ||
                notes.C175WindowForTests != 0u)
            {
                ++StressDriftCount;
            }
            else
            {
                ++StressLifecycleCount;
            }
        }

        bool passed = StressAllocationBytes == 0 && StressDriftCount == 0 &&
            StressRollbackCount == StressFailedGeometryRequests &&
            StressLifecycleCount == StressLifecycleCycles &&
            width == 960 && height == 436;
        host?.TryLog(passed
            ? "C175-NOTES-RESIZE-STRESS layout=1000 alternating=500 failed=250 lifecycle=100 allocationBytes=0 drift=0 result=PASS"u8
            : "C175-NOTES-RESIZE-STRESS result=FAIL"u8);
        return passed;
    }

    private static ReadOnlySpan<byte> MultilineDocument =>
        "line 00\nline 01\nline 02\nline 03\nline 04\nline 05\nline 06\nline 07\nline 08\nline 09\nline 10\nline 11"u8;

    private static bool TryLayout(int width, int height,
        out GuideXosManagedNotesLayoutC175 layout)
    {
        layout = default;
        if (!ManagedNotes.ResizeBoundsC175.TryCreateCandidate(width, height,
                out GuideXosManagedWindowResizeCandidateC174 candidate))
        {
            return false;
        }
        return GuideXosManagedNotesLayoutC175.TryCreate(in candidate,
            out layout);
    }

    private static bool RepeatedGeometryRoundTrip()
    {
        if (!CreateDocument("repeat one\nrepeat two\nrepeat three\nrepeat four\nrepeat five\nrepeat six"u8,
                "/system/apps/C175/repeat.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        int width = 600;
        int height = 360;
        for (int index = 0; index < 20; index++)
        {
            bool toMaximum = (index & 1) == 0;
            if (!TryResize(area, document, ref width, ref height,
                    toMaximum ? 960 : 560, toMaximum ? 436 : 344))
            {
                return false;
            }
        }
        return width == 560 && height == 344 && area.Length == 69 &&
            area.LineCount == 6 && area.IsFocused && !document.Dirty &&
            string.Equals(document.CurrentPath,
                "/system/apps/C175/repeat.txt", StringComparison.Ordinal);
    }

    private static bool DocumentStability(ReadOnlySpan<byte> content,
        string path)
    {
        if (!CreateDocument(content, path, out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        int width = 600;
        int height = 360;
        if (!TryResize(area, document, ref width, ref height, 960, 436) ||
            !TryResize(area, document, ref width, ref height, 560, 344) ||
            !TryResize(area, document, ref width, ref height, 720, 420))
        {
            return false;
        }
        Span<byte> copy = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        return area.TryCopyUtf8To(copy, out int written) &&
            content.SequenceEqual(copy[..written]) &&
            area.IsFocused && !document.Dirty &&
            string.Equals(document.CurrentPath, path, StringComparison.Ordinal) &&
            area.HistoryCount == 1 && area.HistoryIndex == 0 &&
            !area.CanUndo && !area.CanRedo;
    }

    private static bool LongViewportDocumentStability()
    {
        Span<byte> longDocument = stackalloc byte[120];
        int position = 0;
        for (int line = 0; line < 12; line++)
        {
            longDocument[position++] = (byte)'L';
            longDocument[position++] = (byte)('0' + (line / 10));
            longDocument[position++] = (byte)('0' + (line % 10));
            if (line != 11) longDocument[position++] = (byte)'\n';
        }
        if (!CreateDocument(longDocument[..position],
                "/system/apps/C175/long.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        for (int line = 0; line < 8; line++)
        {
            if (area.HandleKey(GuideXosTextInputKey.Down) !=
                    GuideXosTextAreaEditResult.Moved)
            {
                return false;
            }
        }
        int priorOffset = area.FirstVisibleLine;
        int width = 600;
        int height = 360;
        bool resized = TryResize(area, document, ref width, ref height,
            960, 436);
        Span<byte> copy = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        bool contentSame = area.TryCopyUtf8To(copy, out int written) &&
            longDocument[..position].SequenceEqual(copy[..written]);
        return resized && contentSame && area.LineCount == 12 &&
            area.VisibleLineCount == 8 && priorOffset > 0 &&
            area.FirstVisibleLine == Math.Min(priorOffset,
                area.MaximumFirstVisibleLine) && area.CaretLine <
                area.FirstVisibleLine + area.VisibleLineCount &&
            !document.Dirty;
    }

    private static bool EditingStatePreservation()
    {
        if (!CreateDocument("hello from C164"u8,
                "/system/apps/C164/hello.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        area.HandleKey(GuideXosTextInputKey.Right);
        area.HandleKey(GuideXosTextInputKey.Right, shift: true);
        int caret = area.CaretIndex;
        int anchor = area.AnchorIndex;
        int history = area.HistoryCount;
        GuideXosTextRevision revision = area.CurrentRevision;
        int width = 600;
        int height = 360;
        return area.HasSelection && TryResize(area, document,
                ref width, ref height, 720, 420) &&
            area.CaretIndex == caret && area.AnchorIndex == anchor &&
            area.HasSelection && area.IsFocused &&
            area.HistoryCount == history && area.CurrentRevision.Equals(revision) &&
            !document.Dirty && string.Equals(document.CurrentPath,
                "/system/apps/C164/hello.txt", StringComparison.Ordinal);
    }

    private static bool PointerCoordinatesAfterResize()
    {
        if (!CreateDocument("zero one two\nsecond row"u8,
                "/system/apps/C175/pointer.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        int width = 600;
        int height = 360;
        if (!TryResize(area, document, ref width, ref height, 960, 436))
            return false;
        GuideXosManagedNotesLayoutC175 layout;
        if (!TryLayout(width, height, out layout)) return false;
        GuideXosTextAreaEditResult pointer = area.HandlePointerDown(
            GuideXosManagedNotesLayoutC175.EditorX + 5 *
                GuideXosManagedNotesLayoutC175.EditorCharacterWidth,
            GuideXosManagedNotesLayoutC175.EditorY +
                GuideXosManagedNotesLayoutC175.EditorLineHeight,
            GuideXosManagedNotesLayoutC175.EditorX,
            GuideXosManagedNotesLayoutC175.EditorY,
            GuideXosManagedNotesLayoutC175.EditorCharacterWidth,
            GuideXosManagedNotesLayoutC175.EditorLineHeight);
        return pointer == GuideXosTextAreaEditResult.Focused &&
            area.CaretLine == 1 && area.CaretColumn == 5 && area.IsFocused;
    }

    private static bool EditorPointerFocusThenCharacterInsertion()
    {
        if (!CreateDocument("one\ntwo"u8,
                "/system/apps/C175/pointer-insert.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document) || area.IsFocused)
        {
            return false;
        }

        int editorX = GuideXosManagedNotesLayoutC175.EditorX;
        int editorY = GuideXosManagedNotesLayoutC175.EditorY;
        int characterWidth = GuideXosManagedNotesLayoutC175.EditorCharacterWidth;
        int lineHeight = GuideXosManagedNotesLayoutC175.EditorLineHeight;
        GuideXosTextAreaEditResult pointer = area.HandlePointerDown(
            editorX + characterWidth / 2,
            editorY + lineHeight / 2,
            editorX, editorY, characterWidth, lineHeight);
        int beforeLength = area.Length;
        int beforeCaret = area.CaretIndex;
        string beforeText = area.Text;
        GuideXosTextAreaEditResult insertion = area.HandleCharacter('a');

        return pointer == GuideXosTextAreaEditResult.Focused && area.IsFocused &&
            beforeText == "one\ntwo" && beforeLength == 7 && beforeCaret == 0 &&
            insertion == GuideXosTextAreaEditResult.Changed &&
            area.Text == "aone\ntwo" && area.Length == beforeLength + 1 &&
            area.CaretIndex == beforeCaret + 1 && document.Dirty;
    }

    private static bool ControlHostPointerFocusThenCharacterInsertion()
    {
        if (!CreateDocument("one\ntwo"u8,
                "/system/apps/C175/control-host-pointer-insert.txt",
                out GuideXosTextArea area,
                out GuideXosNotesDocumentState document) || area.IsFocused)
        {
            return false;
        }

        GuideXosControlHost host = new(1);
        if (host.TryRegisterTextArea(6, area) !=
                GuideXosControlHostResult.Registered)
        {
            return false;
        }

        GuideXosControlHostResult pointer = host.FocusAndRoutePointer(
            6, 24, 99, 20, 72, 8, 18);
        int beforeLength = area.Length;
        int beforeCaret = area.CaretIndex;
        string beforeText = area.Text;
        GuideXosControlHostResult insertion = host.HandleInput(
            GuideXosInputEvent.ForKeyChar('a'));

        return pointer == GuideXosControlHostResult.Focused && area.IsFocused &&
            host.ActiveControlId == 6 && beforeText == "one\ntwo" &&
            beforeLength == 7 && beforeCaret == 4 &&
            insertion == GuideXosControlHostResult.Changed &&
            area.Text == "one\natwo" && area.Length == beforeLength + 1 &&
            area.CaretIndex == beforeCaret + 1 && document.Dirty;
    }

    private static bool KeyboardNavigationAfterResize()
    {
        if (!CreateDocument("one\ntwo\nthree\nfour\nfive\nsix\nseven"u8,
                "/system/apps/C175/navigation.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        int width = 600;
        int height = 360;
        if (!TryResize(area, document, ref width, ref height, 560, 344))
            return false;
        GuideXosTextAreaEditResult down =
            area.HandleKey(GuideXosTextInputKey.Down);
        GuideXosTextAreaEditResult right =
            area.HandleKey(GuideXosTextInputKey.Right);
        return down == GuideXosTextAreaEditResult.Moved &&
            right == GuideXosTextAreaEditResult.Moved && area.CaretLine == 1 &&
            area.CaretColumn == 1 && area.IsFocused;
    }

    private static bool EditUndoRedoAfterResize()
    {
        if (!CreateDocument("hello from C164"u8,
                "/system/apps/C164/hello.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        area.HandleKey(GuideXosTextInputKey.Right);
        area.HandleKey(GuideXosTextInputKey.Right, shift: true);
        int width = 600;
        int height = 360;
        if (!TryResize(area, document, ref width, ref height, 960, 436) ||
            area.HandleCharacter('X') != GuideXosTextAreaEditResult.Changed ||
            !document.Dirty || !TryResize(area, document, ref width,
                ref height, 560, 344) || !document.Dirty || !area.CanUndo)
        {
            return false;
        }
        bool undo = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            !document.Dirty && area.CanRedo && area.Text == "hello from C164";
        bool redo = TryResize(area, document, ref width, ref height, 720, 420) &&
            area.CanRedo && document.Dirty == false &&
            area.Redo() == GuideXosTextAreaEditResult.Changed && document.Dirty &&
            area.Text == "hXllo from C164";
        return undo && redo;
    }

    private static bool DirtyStateAndPathPreservation()
    {
        if (!CreateDocument("dirty baseline"u8,
                "/system/apps/C175/dirty.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.HandleCharacter('!');
        if (!document.Dirty) return false;
        GuideXosTextRevision revision = area.CurrentRevision;
        int historyIndex = area.HistoryIndex;
        string path = document.CurrentPath;
        int width = 600;
        int height = 360;
        return TryResize(area, document, ref width, ref height, 960, 436) &&
            TryResize(area, document, ref width, ref height, 560, 344) &&
            document.Dirty && area.CurrentRevision.Equals(revision) &&
            area.HistoryIndex == historyIndex &&
            string.Equals(document.CurrentPath, path, StringComparison.Ordinal);
    }

    private static bool ScrollPositionAdjustment()
    {
        if (!CreateDocument(MultilineDocument,
                "/system/apps/C175/scroll.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        for (int line = 0; line < 8; line++)
            area.HandleKey(GuideXosTextInputKey.Down);
        int priorOffset = area.FirstVisibleLine;
        int width = 600;
        int height = 360;
        if (priorOffset == 0 || !TryResize(area, document, ref width,
                ref height, 960, 436))
        {
            return false;
        }
        bool expanded = area.FirstVisibleLine == Math.Min(priorOffset,
                area.MaximumFirstVisibleLine) &&
            area.FirstVisibleLine <= area.MaximumFirstVisibleLine && area.CaretLine <
                area.FirstVisibleLine + area.VisibleLineCount;
        if (!expanded || !TryResize(area, document, ref width, ref height,
                560, 344))
        {
            return false;
        }
        int requiredOffset = Math.Max(0,
            area.CaretLine - area.VisibleLineCount + 1);
        return area.FirstVisibleLine == requiredOffset &&
            area.FirstVisibleLine <= area.MaximumFirstVisibleLine &&
            area.CaretLine < area.FirstVisibleLine + area.VisibleLineCount;
    }

    private static bool ExactRollbackAfterRejectedTransaction()
    {
        if (!CreateDocument(MultilineDocument,
                "/system/apps/C175/rollback.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        area.Focus();
        area.SetCaretToStart();
        for (int line = 0; line < 6; line++)
            area.HandleKey(GuideXosTextInputKey.Down);
        area.HandleKey(GuideXosTextInputKey.Left, shift: true);
        if (!area.TrySetViewportDimensions(27, 4))
        {
            return false;
        }
        int width = 560;
        int height = 344;
        int priorOffset = area.FirstVisibleLine;
        int priorCaret = area.CaretIndex;
        int priorAnchor = area.AnchorIndex;
        int priorPreferredColumn = area.PreferredColumn;
        int priorColumns = area.MaximumRenderableColumns;
        int priorLines = area.VisibleLineCount;
        int priorHistory = area.HistoryCount;
        int priorHistoryIndex = area.HistoryIndex;
        GuideXosTextRevision priorRevision = area.CurrentRevision;
        bool priorUndo = area.CanUndo;
        bool priorRedo = area.CanRedo;
        bool priorFocus = area.IsFocused;
        Span<byte> priorText = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        if (!area.TryCopyUtf8To(priorText, out int priorLength)) return false;
        GuideXosManagedWindowResizeTransactionC174 failure = new(
            in ManagedNotes.ResizeBoundsC175, width, height, 960, 436);
        GuideXosManagedWindowResizeCandidateC174 candidate = failure.Candidate;
        if (!failure.IsValid ||
            !GuideXosManagedNotesLayoutC175.TryCreate(in candidate,
                out GuideXosManagedNotesLayoutC175 rejectedLayout) ||
            !area.TrySetViewportDimensions(rejectedLayout.EditorColumns,
                rejectedLayout.EditorLines) ||
            (area.MaximumRenderableColumns == priorColumns &&
                area.VisibleLineCount == priorLines) ||
            failure.Complete(true,
                GuideXosResult.CapabilityUnavailable, true) ||
            !failure.RequiresRollback || failure.Result !=
                GuideXosResult.CapabilityUnavailable)
        {
            return false;
        }
        if (!area.TrySetViewportDimensions(priorColumns, priorLines) ||
            (!area.SetFirstVisibleLine(priorOffset) &&
                area.FirstVisibleLine != priorOffset))
        {
            return false;
        }
        Span<byte> restoredText = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        return area.TryCopyUtf8To(restoredText, out int restoredLength) &&
            restoredLength == priorLength &&
            priorText[..priorLength].SequenceEqual(restoredText[..restoredLength]) &&
            area.CaretIndex == priorCaret && area.AnchorIndex == priorAnchor &&
            area.PreferredColumn == priorPreferredColumn &&
            area.FirstVisibleLine == priorOffset &&
            area.MaximumRenderableColumns == priorColumns &&
            area.VisibleLineCount == priorLines &&
            area.HistoryCount == priorHistory &&
            area.HistoryIndex == priorHistoryIndex &&
            area.CurrentRevision.Equals(priorRevision) &&
            area.CanUndo == priorUndo && area.CanRedo == priorRedo &&
            area.IsFocused == priorFocus && !document.Dirty &&
            width == 560 && height == 344;
    }

    private static bool RepeatedFailedGeometryRequests()
    {
        if (!CreateDocument("reject safe"u8,
                "/system/apps/C175/reject.txt", out GuideXosTextArea area,
                out GuideXosNotesDocumentState document))
        {
            return false;
        }
        int width = 600;
        int height = 360;
        for (int index = 0; index < 100; index++)
        {
            GuideXosManagedWindowResizeTransactionC174 transaction = new(
                in ManagedNotes.ResizeBoundsC175, width, height,
                (index & 1) == 0 ? 559 : 960,
                (index & 1) == 0 ? 360 : 437);
            if (transaction.IsValid || transaction.RequiresRollback ||
                width != 600 || height != 360 || area.Length != 11 ||
                document.Dirty)
            {
                return false;
            }
        }
        return true;
    }

    private static bool CreateDocument(ReadOnlySpan<byte> content,
        string path, out GuideXosTextArea area,
        out GuideXosNotesDocumentState document)
    {
        area = new GuideXosTextArea(256, 32, 4, 32);
        document = new GuideXosNotesDocumentState();
        return document.TryHydrate(area, path, content);
    }

    internal static bool TryResizeForC176(GuideXosTextArea area,
        GuideXosNotesDocumentState document, ref int width, ref int height,
        int nextWidth, int nextHeight) =>
        TryResize(area, document, ref width, ref height, nextWidth, nextHeight);

    private static bool TryResize(GuideXosTextArea area,
        GuideXosNotesDocumentState document, ref int width, ref int height,
        int nextWidth, int nextHeight)
    {
        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in ManagedNotes.ResizeBoundsC175, width, height,
            nextWidth, nextHeight);
        if (!transaction.IsValid) return false;

        int priorCaret = area.CaretIndex;
        int priorAnchor = area.AnchorIndex;
        int priorPreferredColumn = area.PreferredColumn;
        int priorColumns = area.MaximumRenderableColumns;
        int priorLines = area.VisibleLineCount;
        int priorOffset = area.FirstVisibleLine;
        int priorHistoryCount = area.HistoryCount;
        int priorHistoryIndex = area.HistoryIndex;
        GuideXosTextRevision priorRevision = area.CurrentRevision;
        bool priorUndo = area.CanUndo;
        bool priorRedo = area.CanRedo;
        bool priorFocus = area.IsFocused;
        bool priorDirty = document.Dirty;
        string priorPath = document.CurrentPath;
        Span<byte> priorText = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        if (!area.TryCopyUtf8To(priorText, out int priorLength)) return false;

        GuideXosManagedWindowResizeCandidateC174 candidate =
            transaction.Candidate;
        if (!GuideXosManagedNotesLayoutC175.TryCreate(in candidate,
                out GuideXosManagedNotesLayoutC175 layout) ||
            !area.TrySetViewportDimensions(layout.EditorColumns,
                layout.EditorLines))
        {
            _ = transaction.Complete(false, GuideXosResult.Success, false);
            _ = area.TrySetViewportDimensions(priorColumns, priorLines);
            _ = area.SetFirstVisibleLine(priorOffset);
            return false;
        }

        int expectedOffset = Math.Min(priorOffset,
            area.MaximumFirstVisibleLine);
        if (area.CaretLine < expectedOffset)
            expectedOffset = area.CaretLine;
        else if (area.CaretLine >= expectedOffset + area.VisibleLineCount)
            expectedOffset = area.CaretLine - area.VisibleLineCount + 1;
        expectedOffset = Math.Clamp(expectedOffset, 0,
            area.MaximumFirstVisibleLine);

        Span<byte> currentText = stackalloc byte[GuideXosTextArea.MaximumSupportedCharacters];
        bool preserved = area.TryCopyUtf8To(currentText,
                out int currentLength) && currentLength == priorLength &&
            priorText[..priorLength].SequenceEqual(currentText[..currentLength]) &&
            area.CaretIndex == priorCaret && area.AnchorIndex == priorAnchor &&
            area.PreferredColumn == priorPreferredColumn &&
            area.FirstVisibleLine == expectedOffset &&
            area.HistoryCount == priorHistoryCount &&
            area.HistoryIndex == priorHistoryIndex &&
            area.CurrentRevision.Equals(priorRevision) &&
            area.CanUndo == priorUndo && area.CanRedo == priorRedo &&
            area.IsFocused == priorFocus && document.Dirty == priorDirty &&
            string.Equals(document.CurrentPath, priorPath,
                StringComparison.Ordinal);
        if (!transaction.Complete(true, GuideXosResult.Success, preserved))
        {
            _ = area.TrySetViewportDimensions(priorColumns, priorLines);
            _ = area.SetFirstVisibleLine(priorOffset);
            return false;
        }
        width = transaction.Candidate.OuterWidth;
        height = transaction.Candidate.OuterHeight;
        return true;
    }

    private static bool TryRejectedResizeWithRollback(
        GuideXosTextArea area, GuideXosNotesDocumentState document,
        int priorWidth, int priorHeight, int requestedWidth,
        int requestedHeight)
    {
        GuideXosManagedWindowResizeTransactionC174 transaction = new(
            in ManagedNotes.ResizeBoundsC175, priorWidth, priorHeight,
            requestedWidth, requestedHeight);
        if (!transaction.IsValid) return false;

        int priorCaret = area.CaretIndex;
        int priorAnchor = area.AnchorIndex;
        int priorPreferredColumn = area.PreferredColumn;
        int priorColumns = area.MaximumRenderableColumns;
        int priorLines = area.VisibleLineCount;
        int priorOffset = area.FirstVisibleLine;
        int priorHistoryCount = area.HistoryCount;
        int priorHistoryIndex = area.HistoryIndex;
        GuideXosTextRevision priorRevision = area.CurrentRevision;
        bool priorUndo = area.CanUndo;
        bool priorRedo = area.CanRedo;
        bool priorFocus = area.IsFocused;
        bool priorDirty = document.Dirty;
        string priorPath = document.CurrentPath;
        Span<byte> priorText = stackalloc byte[
            GuideXosTextArea.MaximumSupportedCharacters];
        if (!area.TryCopyUtf8To(priorText, out int priorLength)) return false;

        GuideXosManagedWindowResizeCandidateC174 candidate =
            transaction.Candidate;
        if (!GuideXosManagedNotesLayoutC175.TryCreate(in candidate,
                out GuideXosManagedNotesLayoutC175 nextLayout) ||
            !area.TrySetViewportDimensions(nextLayout.EditorColumns,
                nextLayout.EditorLines))
        {
            return false;
        }
        if (priorColumns == area.MaximumRenderableColumns &&
            priorLines == area.VisibleLineCount)
        {
            _ = area.TrySetViewportDimensions(priorColumns, priorLines);
            _ = area.SetFirstVisibleLine(priorOffset);
            return false;
        }

        if (transaction.Complete(true,
                GuideXosResult.CapabilityUnavailable, true) ||
            !transaction.RequiresRollback)
        {
            _ = area.TrySetViewportDimensions(priorColumns, priorLines);
            _ = area.SetFirstVisibleLine(priorOffset);
            return false;
        }
        if (!area.TrySetViewportDimensions(priorColumns, priorLines) ||
            (!area.SetFirstVisibleLine(priorOffset) &&
                area.FirstVisibleLine != priorOffset))
        {
            return false;
        }

        Span<byte> restoredText = stackalloc byte[
            GuideXosTextArea.MaximumSupportedCharacters];
        return area.TryCopyUtf8To(restoredText, out int restoredLength) &&
            restoredLength == priorLength &&
            priorText[..priorLength].SequenceEqual(
                restoredText[..restoredLength]) &&
            area.CaretIndex == priorCaret && area.AnchorIndex == priorAnchor &&
            area.PreferredColumn == priorPreferredColumn &&
            area.FirstVisibleLine == priorOffset &&
            area.MaximumRenderableColumns == priorColumns &&
            area.VisibleLineCount == priorLines &&
            area.HistoryCount == priorHistoryCount &&
            area.HistoryIndex == priorHistoryIndex &&
            area.CurrentRevision.Equals(priorRevision) &&
            area.CanUndo == priorUndo && area.CanRedo == priorRedo &&
            area.IsFocused == priorFocus && document.Dirty == priorDirty &&
            string.Equals(document.CurrentPath, priorPath,
                StringComparison.Ordinal);
    }

    private static bool Case(ref int cases, bool condition)
    {
        ++cases;
        if (condition) return true;
        if (FirstFailedCase == 0) FirstFailedCase = cases;
        return false;
    }
}
#endif
