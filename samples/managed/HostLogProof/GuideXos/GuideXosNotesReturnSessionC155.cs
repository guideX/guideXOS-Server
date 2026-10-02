#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace HostLogProof;

internal enum GuideXosNotesSessionDecisionC155 : byte
{
    Unresolved = 0,
    Clean = 1,
    Saved = 2,
    Discarded = 3,
    Cancelled = 4,
}

internal readonly struct GuideXosNotesReturnSessionDataC155
{
    public GuideXosNotesReturnSessionDataC155(bool valid, bool hasPath,
        string path, int caretIndex, int anchorIndex, int firstVisibleLine,
        uint generation)
    {
        IsValid = valid;
        HasPath = hasPath;
        Path = path;
        CaretIndex = caretIndex;
        AnchorIndex = anchorIndex;
        FirstVisibleLine = firstVisibleLine;
        Generation = generation;
    }

    public bool IsValid { get; }
    public bool HasPath { get; }
    public string Path { get; }
    public int CaretIndex { get; }
    public int AnchorIndex { get; }
    public int FirstVisibleLine { get; }
    public uint Generation { get; }
}

/// <summary>
/// One RAM-only C150 return slot. The fixed slot stores a canonical VFS path
/// and logical view positions; it never owns document text or Notes objects.
/// </summary>
internal sealed class GuideXosNotesReturnSessionC155
{
    public const int PathCapacityBytes = (int)GxAbi.FilePathMaxBytes;
    public const uint NativeClearActionId = 0xC15506u;
    public const uint NativeCheckActionId = 0xC15507u;
    private const uint ValidTag = 0xC1550001u;
    private const byte HasPathFlag = 1;
    private static readonly GuideXosNotesReturnSessionC155 s_shared = new();

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private unsafe struct Slot
    {
        public uint Tag;
        public uint Generation;
        public int CaretIndex;
        public int AnchorIndex;
        public int FirstVisibleLine;
        public byte Flags;
        public fixed byte Path[PathCapacityBytes + 1];
    }

    private Slot _slot;

    public static GuideXosNotesReturnSessionC155 Shared => s_shared;
    public bool IsPending => _slot.Tag != 0u;
    public uint Generation => _slot.Generation;
    public const int FixedStorageBytes = 118;

    public bool TryCaptureResolved(
        GuideXosNotesDocumentState documentState,
        GuideXosTextArea textArea,
        GuideXosNotesSessionDecisionC155 decision,
        out uint generation)
    {
        generation = _slot.Generation;
        if (documentState == null || textArea == null || IsPending ||
            decision is GuideXosNotesSessionDecisionC155.Unresolved or
                GuideXosNotesSessionDecisionC155.Cancelled)
            return false;

        if (decision == GuideXosNotesSessionDecisionC155.Clean &&
            (documentState.Dirty || !documentState.HasSavedRevision ||
                !documentState.SavedRevisionReachable))
            return false;
        if (decision == GuideXosNotesSessionDecisionC155.Saved &&
            (documentState.Dirty || !documentState.HasCurrentPath ||
                !documentState.HasSavedRevision ||
                !documentState.SavedRevisionReachable))
            return false;
        if (decision == GuideXosNotesSessionDecisionC155.Discarded &&
            !documentState.Dirty)
            return false;

        string path = documentState.HasCurrentPath
            ? documentState.CurrentPath : null;
        int caret = path == null ? 0 : textArea.CaretIndex;
        int anchor = path == null ? 0 : textArea.AnchorIndex;
        int firstVisibleLine = path == null ? 0 : textArea.FirstVisibleLine;
        return TryArm(path, caret, anchor, firstVisibleLine, out generation);
    }

    public unsafe bool TryArm(string path, int caretIndex, int anchorIndex,
        int firstVisibleLine, out uint generation)
    {
        generation = _slot.Generation;
        if (IsPending || caretIndex < 0 || anchorIndex < 0 ||
            firstVisibleLine < 0)
            return false;

        Span<byte> encodedPath = stackalloc byte[PathCapacityBytes];
        int pathLength = 0;
        bool hasPath = !string.IsNullOrEmpty(path);
        if (hasPath)
        {
            if (!IsValidPath(path)) return false;
            pathLength = Encoding.UTF8.GetByteCount(path);
            if (pathLength < 1 || pathLength > PathCapacityBytes ||
                Encoding.UTF8.GetBytes(path.AsSpan(), encodedPath) != pathLength)
                return false;
        }

        uint nextGeneration = unchecked(_slot.Generation + 1u);
        if (nextGeneration == 0u) nextGeneration = 1u;
        _slot.Generation = nextGeneration;
        _slot.CaretIndex = caretIndex;
        _slot.AnchorIndex = anchorIndex;
        _slot.FirstVisibleLine = firstVisibleLine;
        _slot.Flags = hasPath ? HasPathFlag : (byte)0u;
        fixed (byte* destination = _slot.Path)
        {
            for (int index = 0; index < pathLength; index++)
                destination[index] = encodedPath[index];
            destination[pathLength] = 0;
            for (int index = pathLength + 1; index <= PathCapacityBytes; index++)
                destination[index] = 0;
        }
        _slot.Tag = ValidTag;
        generation = nextGeneration;
        return true;
    }

    /// <summary>
    /// A mismatched return leaves the slot untouched. A matching Notes return
    /// consumes it once even when its tag or bounded path is malformed.
    /// </summary>
    public unsafe bool TryConsume(bool returnTargetIsNotes,
        out GuideXosNotesReturnSessionDataC155 data)
    {
        data = default;
        if (!returnTargetIsNotes || _slot.Tag == 0u) return false;

        uint generation = _slot.Generation;
        bool hasPath = (_slot.Flags & HasPathFlag) != 0u;
        bool valid = _slot.Tag == ValidTag && generation != 0u &&
            (_slot.Flags & ~HasPathFlag) == 0u && _slot.CaretIndex >= 0 &&
            _slot.AnchorIndex >= 0 && _slot.FirstVisibleLine >= 0;
        string path = null;
        if (valid && hasPath)
        {
            fixed (byte* source = _slot.Path)
            {
                int length = 0;
                while (length <= PathCapacityBytes && source[length] != 0u)
                    ++length;
                if (length == 0 || length > PathCapacityBytes)
                {
                    valid = false;
                }
                else
                {
                    path = Encoding.UTF8.GetString(
                        new ReadOnlySpan<byte>(source, length));
                    valid = IsValidPath(path);
                }
            }
        }
        else if (valid)
        {
            fixed (byte* source = _slot.Path)
            {
                for (int index = 0; index <= PathCapacityBytes; index++)
                {
                    if (source[index] != 0u)
                    {
                        valid = false;
                        break;
                    }
                }
            }
        }

        data = new GuideXosNotesReturnSessionDataC155(valid, hasPath,
            path, _slot.CaretIndex, _slot.AnchorIndex,
            _slot.FirstVisibleLine, generation);
        Clear();
        return true;
    }

    public void Clear()
    {
        uint generation = _slot.Generation;
        _slot = default;
        _slot.Generation = generation;
    }

    private static bool IsValidPath(string path)
    {
        if (string.IsNullOrEmpty(path) ||
            Encoding.UTF8.GetByteCount(path) > PathCapacityBytes)
            return false;
        int separator = path.LastIndexOf('/');
        return separator >= GuideXosOpenFileDialog.ManagedVfsRoot.Length - 1 &&
            GuideXosPickerPath.TryBuildPath(path.Substring(0, separator),
                path.Substring(separator + 1), out string normalized) ==
                GuideXosPickerPathStatus.Success &&
            string.Equals(path, normalized, StringComparison.Ordinal);
    }
}

public static class GuideXosNotesReturnSessionC155Tests
{
    public const int SessionCaseCount = 16;
    public const int CaptureCaseCount = 11;
    public const int TextAreaCaseCount = 4;
    public const int TotalCaseCount = SessionCaseCount + CaptureCaseCount +
        TextAreaCaseCount;

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool all = RunSessionCases(ref cases) && RunCaptureCases(ref cases) &&
            RunTextAreaCases(ref cases);
        bool countCorrect = cases == TotalCaseCount;
        host?.TryLog(all && countCorrect
            ? "C155-SESSION-CORE cases=31 slot=one-shot bounded=PASS result=PASS"u8
            : "C155-SESSION-CORE result=FAIL"u8);
        return all && countCorrect;
    }

    private static bool RunSessionCases(ref int cases)
    {
        bool all = true;
        GuideXosNotesReturnSessionC155 slot = new();
        all &= Case(ref cases, !slot.IsPending && slot.Generation == 0u);

        bool namedArm = slot.TryArm("/system/apps/C155/alpha.txt", 73, 61,
            7, out uint generation) && slot.IsPending && generation == 1u;
        all &= Case(ref cases, namedArm);

        bool occupiedRejected = !slot.TryArm("/system/apps/C155/beta.txt",
            0, 0, 0, out _);
        all &= Case(ref cases, occupiedRejected);

        bool mismatchedTarget = !slot.TryConsume(false, out _) && slot.IsPending;
        all &= Case(ref cases, mismatchedTarget);

        bool namedConsumed = slot.TryConsume(true,
                out GuideXosNotesReturnSessionDataC155 named) && named.IsValid &&
            named.HasPath && named.Path == "/system/apps/C155/alpha.txt" &&
            named.CaretIndex == 73 && named.AnchorIndex == 61 &&
            named.FirstVisibleLine == 7 && named.Generation == 1u &&
            !slot.IsPending;
        all &= Case(ref cases, namedConsumed);

        bool secondConsumeEmpty = !slot.TryConsume(true, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, secondConsumeEmpty);

        bool blankArm = slot.TryArm(null, 0, 0, 0, out uint blankGeneration) &&
            blankGeneration == 2u;
        all &= Case(ref cases, blankArm);

        bool blankConsumed = slot.TryConsume(true,
                out GuideXosNotesReturnSessionDataC155 blank) && blank.IsValid &&
            !blank.HasPath && string.IsNullOrEmpty(blank.Path) &&
            blank.Generation == 2u;
        all &= Case(ref cases, blankConsumed);

        string maxPath = "/system/apps/" + new string('x',
            GuideXosNotesReturnSessionC155.PathCapacityBytes -
                "/system/apps/".Length);
        bool maximumPathAccepted = slot.TryArm(maxPath, 1, 0, 0, out _) &&
            slot.TryConsume(true,
                out GuideXosNotesReturnSessionDataC155 maximum) &&
            maximum.IsValid && maximum.Path == maxPath;
        all &= Case(ref cases, maximumPathAccepted);

        string longPath = "/system/apps/" + new string('x',
            GuideXosNotesReturnSessionC155.PathCapacityBytes + 1 -
                "/system/apps/".Length);
        bool overlongRejected = !slot.TryArm(longPath, 0, 0, 0, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, overlongRejected);

        bool invalidPathRejected = !slot.TryArm("/system/../outside.txt",
            0, 0, 0, out _) && !slot.IsPending;
        all &= Case(ref cases, invalidPathRejected);

        bool invalidPositionsRejected = !slot.TryArm(null, -1, 0, 0, out _) &&
            !slot.TryArm(null, 0, 0, -1, out _) && !slot.IsPending;
        all &= Case(ref cases, invalidPositionsRejected);

        bool clearWorks = slot.TryArm("/system/apps/C155/a.txt", 0, 0, 0,
                out uint beforeClear) && beforeClear == 4u;
        slot.Clear();
        clearWorks &= !slot.IsPending && slot.Generation == beforeClear;
        all &= Case(ref cases, clearWorks);

        bool generationAdvances = slot.TryArm(null, 0, 0, 0,
                out uint afterClear) && afterClear == beforeClear + 1u;
        slot.Clear();
        all &= Case(ref cases, generationAdvances);

        bool exactSlotSize = GuideXosNotesReturnSessionC155.FixedStorageBytes ==
            118;
        all &= Case(ref cases, exactSlotSize);

        bool cycleStress = true;
        for (int index = 0; index < 25; index++)
        {
            cycleStress &= slot.TryArm("/system/apps/C155/alpha.txt",
                index, index, index, out uint armedGeneration);
            cycleStress &= slot.TryConsume(true,
                out GuideXosNotesReturnSessionDataC155 cycle) &&
                cycle.IsValid && cycle.Generation == armedGeneration &&
                cycle.CaretIndex == index && cycle.AnchorIndex == index &&
                cycle.FirstVisibleLine == index && !slot.IsPending;
        }
        all &= Case(ref cases, cycleStress);
        return all;
    }

    private static bool RunCaptureCases(ref int cases)
    {
        bool all = true;
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();
        GuideXosNotesReturnSessionC155 slot = new();

        bool cleanUntitled = state.InitializeUntitled(area) && !state.Dirty &&
            slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Clean, out _) &&
            slot.TryConsume(true, out var untitled) && untitled.IsValid &&
            !untitled.HasPath && untitled.CaretIndex == 0;
        all &= Case(ref cases, cleanUntitled);

        bool cleanNamed = state.TryHydrate(area, "/system/apps/C155/a.txt",
                "ABC"u8) && !state.Dirty && slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Clean, out _) &&
            slot.TryConsume(true, out var named) && named.HasPath &&
            named.Path == "/system/apps/C155/a.txt";
        all &= Case(ref cases, cleanNamed);

        area.Focus();
        area.HandleKey(GuideXosTextInputKey.End, false);
        bool unresolvedDirtyRejected = area.HandleCharacter('D') ==
                GuideXosTextAreaEditResult.Changed && state.Dirty &&
            !slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Unresolved, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, unresolvedDirtyRejected);

        bool dirtySaveBeforeWriteRejected = !slot.TryCaptureResolved(state,
            area, GuideXosNotesSessionDecisionC155.Saved, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, dirtySaveBeforeWriteRejected);

        bool discardNamed = slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Discarded, out _) &&
            slot.TryConsume(true, out var discarded) && discarded.IsValid &&
            discarded.Path == "/system/apps/C155/a.txt" &&
            discarded.CaretIndex == area.CaretIndex;
        all &= Case(ref cases, discardNamed);

        bool dirtyCancelRejected = !slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Cancelled, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, dirtyCancelRejected);

        bool verifiedSaveCapture = state.MarkSaveSucceeded(
                "/system/apps/C155/a.txt") && !state.Dirty &&
            slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Saved, out _) &&
            slot.TryConsume(true, out var saved) && saved.Path ==
                "/system/apps/C155/a.txt";
        all &= Case(ref cases, verifiedSaveCapture);

        bool saveAsCapture = state.TrySetCurrentPath(
                "/system/apps/C155/b.txt") && !state.Dirty &&
            slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Saved, out _) &&
            slot.TryConsume(true, out var saveAs) && saveAs.Path ==
                "/system/apps/C155/b.txt";
        all &= Case(ref cases, saveAsCapture);

        bool saveAsCancelKeepsDirty = area.HandleCharacter('E') ==
                GuideXosTextAreaEditResult.Changed && state.Dirty &&
            !slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Cancelled, out _) &&
            !slot.IsPending && state.Dirty;
        all &= Case(ref cases, saveAsCancelKeepsDirty);

        bool undoToClean = area.Undo() == GuideXosTextAreaEditResult.Changed &&
            !state.Dirty && slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Clean, out _) &&
            slot.TryConsume(true, out var undoClean) && undoClean.IsValid;
        all &= Case(ref cases, undoToClean);

        bool redoToDirty = area.Redo() == GuideXosTextAreaEditResult.Changed &&
            state.Dirty && !slot.TryCaptureResolved(state, area,
                GuideXosNotesSessionDecisionC155.Clean, out _) &&
            !slot.IsPending;
        all &= Case(ref cases, redoToDirty);

        return all;
    }

    private static bool RunTextAreaCases(ref int cases)
    {
        bool all = true;
        GuideXosTextArea area = new(256, 32, 4, 32);
        byte[] manyLines = Encoding.ASCII.GetBytes(string.Concat(
            "line00\nline01\nline02\nline03\nline04\nline05\n" +
            "line06\nline07\nline08\nline09\nline10\nline11\n" +
            "line12\nline13\nline14\nline15\nline16\nline17\n" +
            "line18\nline19\nline20\nline21\nline22\nline23\n" +
            "line24\nline25\nline26\nline27\nline28\nline29"));

        bool loadedBaseline = area.SetUtf8(manyLines) && !area.CanUndo &&
            !area.CanRedo && area.HistoryCount == 1;
        area.RestoreSessionPosition(73, 61, 5);
        bool restoredSelectionAndReveal = area.CaretIndex == 73 &&
            area.AnchorIndex == 61 && area.FirstVisibleLine >= 5 &&
            area.CaretLine >= area.FirstVisibleLine &&
            area.CaretLine < area.FirstVisibleLine + area.VisibleLineCount &&
            !area.CanUndo && !area.CanRedo && area.HistoryCount == 1;
        all &= Case(ref cases, loadedBaseline && restoredSelectionAndReveal);

        area.RestoreSessionPosition(45, 43, 5);
        bool validViewportPreserved = area.CaretIndex == 45 &&
            area.AnchorIndex == 43 && area.FirstVisibleLine == 5;
        all &= Case(ref cases, validViewportPreserved);

        bool shorterLoad = area.SetUtf8("a\nb\n"u8);
        area.RestoreSessionPosition(73, 61, 99);
        bool shortenedClamped = shorterLoad && area.CaretIndex == 4 &&
            area.AnchorIndex == 4 && area.FirstVisibleLine == 0 &&
            area.FirstVisibleLine <= area.MaximumFirstVisibleLine &&
            !area.CanUndo && !area.CanRedo && area.HistoryCount == 1;
        all &= Case(ref cases, shortenedClamped);

        bool emptyLoad = area.SetUtf8(ReadOnlySpan<byte>.Empty);
        area.RestoreSessionPosition(4, 2, 3);
        bool emptyClamped = emptyLoad && area.CaretIndex == 0 &&
            area.AnchorIndex == 0 && area.FirstVisibleLine == 0 &&
            !area.CanUndo && !area.CanRedo && area.HistoryCount == 1;
        all &= Case(ref cases, emptyClamped);
        return all;
    }

    private static bool Case(ref int cases, bool passed)
    {
        ++cases;
        return passed;
    }
}
#endif
