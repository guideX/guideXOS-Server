#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
using System;

namespace HostLogProof.Applications;

public sealed partial class ManagedNotes
{
    private static bool s_c155SessionTestsRun;
    private static bool s_c155RestorationTestsRun;

    private bool TryArmC155ReturnSession(GuideXosHost host,
        GuideXosNotesSessionDecisionC155 decision)
    {
        if (!_c152Enabled || _c152DocumentState == null || _textArea == null)
            return false;

        GuideXosNotesReturnSessionC155 slot =
            GuideXosNotesReturnSessionC155.Shared;
        if (!slot.TryCaptureResolved(_c152DocumentState, _textArea,
                decision, out uint generation))
        {
            host.TryLog("C155-SESSION-ARMED result=FAIL"u8);
            _status = "Unable to preserve Notes position";
            return false;
        }

        Span<byte> line = stackalloc byte[128];
        int position = 0;
        bool logged = GuideXosText.Append(line, ref position,
                "C155-SESSION-ARMED generation="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, generation) &&
            GuideXosText.Append(line, ref position,
                " path="u8) &&
            GuideXosText.Append(line, ref position,
                _c152DocumentState.HasCurrentPath ? "named"u8 : "blank"u8) &&
            GuideXosText.Append(line, ref position, " caret="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)_textArea.CaretIndex) &&
            GuideXosText.Append(line, ref position, " anchor="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)_textArea.AnchorIndex) &&
            GuideXosText.Append(line, ref position, " viewport="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)_textArea.FirstVisibleLine) &&
            GuideXosText.Append(line, ref position,
                " fixed-bytes=118 result=PASS"u8);
        if (!logged)
        {
            slot.Clear();
            return false;
        }
        host.TryLog(line[..position]);
        return true;
    }

    private bool TryRestoreC155Session(GuideXosHost host,
        GuideXosNotesReturnSessionDataC155 session,
        out GuideXosFileResult fileResult)
    {
        fileResult = GuideXosFileResult.Success;
        _currentPath = string.Empty;
        if (!_c152DocumentState.InitializeUntitled(_textArea))
        {
            fileResult = GuideXosFileResult.InvalidArgument;
            return false;
        }

        bool restored;
        if (!session.IsValid)
        {
            fileResult = GuideXosFileResult.InvalidPath;
            restored = false;
        }
        else if (session.HasPath)
        {
            restored = TryLoadC151Document(host, session.Path,
                out fileResult, out _);
        }
        else
        {
            restored = true;
            _status = "Untitled note";
        }

        if (!restored)
        {
            _currentPath = string.Empty;
            _c152DocumentState.InitializeUntitled(_textArea);
            _status = "The previous document could not be reopened";
            UpdatePathLabel();
            return false;
        }

        _textArea.RestoreSessionPosition(session.CaretIndex,
            session.AnchorIndex, session.FirstVisibleLine);
        _status = session.HasPath
            ? "Restored from VFS" : "Untitled note";
        return true;
    }

    private static void LogC155SessionConsumed(GuideXosHost host,
        GuideXosNotesReturnSessionDataC155 session)
    {
        Span<byte> line = stackalloc byte[128];
        int position = 0;
        bool logged = GuideXosText.Append(line, ref position,
                "C155-SESSION-CONSUMED generation="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                session.Generation) &&
            GuideXosText.Append(line, ref position,
                " target=Notes one-shot=true pending=none"u8) &&
            GuideXosText.Append(line, ref position,
                session.IsValid ? " valid=true result=PASS"u8 :
                    " valid=false result=PASS"u8);
        host.TryLog(logged ? line[..position] :
            "C155-SESSION-CONSUMED result=FAIL"u8);
    }

    private void LogC155Restoration(GuideXosHost host,
        GuideXosNotesReturnSessionDataC155 session, bool documentRestored,
        GuideXosFileResult fileResult)
    {
        bool pathMatches = session.HasPath
            ? _c152DocumentState.HasCurrentPath &&
                string.Equals(_c152DocumentState.CurrentPath, session.Path,
                    StringComparison.Ordinal)
            : !_c152DocumentState.HasCurrentPath;
        bool freshBaseline = !_c152DocumentState.Dirty &&
            !_textArea.CanUndo && !_textArea.CanRedo &&
            _c152DocumentState.HasSavedRevision &&
            _c152DocumentState.SavedRevision == _textArea.CurrentRevision &&
            _c152DocumentState.SavedRevisionReachable;
        bool boundedView = _textArea.CaretIndex >= 0 &&
            _textArea.CaretIndex <= _textArea.Length &&
            _textArea.AnchorIndex >= 0 && _textArea.AnchorIndex <= _textArea.Length &&
            _textArea.FirstVisibleLine >= 0 &&
            _textArea.FirstVisibleLine <= _textArea.MaximumFirstVisibleLine &&
            _textArea.CaretLine >= _textArea.FirstVisibleLine &&
            _textArea.CaretLine < _textArea.FirstVisibleLine +
                _textArea.VisibleLineCount;
        bool sessionConsumed = !GuideXosNotesReturnSessionC155.Shared.IsPending;
        bool blankFallbackSafe = !_c152DocumentState.HasCurrentPath &&
            !_c152DocumentState.Dirty && _textArea.Length == 0 &&
            !_textArea.CanUndo && !_textArea.CanRedo &&
            _textArea.CaretIndex == 0 && _textArea.AnchorIndex == 0 &&
            _textArea.FirstVisibleLine == 0 && sessionConsumed;

        if (documentRestored && session.IsValid && pathMatches &&
            freshBaseline && boundedView && sessionConsumed)
        {
            Span<byte> pathLine = stackalloc byte[64];
            int position = 0;
            bool pathLogged = GuideXosText.Append(pathLine, ref position,
                    "C155-RESTORE generation="u8) &&
                GuideXosText.AppendUnsigned(pathLine, ref position,
                    session.Generation) &&
                GuideXosText.Append(pathLine, ref position,
                    session.HasPath ? " source=VFS named=true"u8 :
                        " source=blank named=false"u8) &&
                GuideXosText.Append(pathLine, ref position,
                    " result=PASS"u8);
            host.TryLog(pathLogged ? pathLine[..position] :
                "C155-RESTORE result=FAIL"u8);

            Span<byte> viewLine = stackalloc byte[128];
            position = 0;
            bool viewLogged = GuideXosText.Append(viewLine, ref position,
                    "C155-RESTORE-VIEW generation="u8) &&
                GuideXosText.AppendUnsigned(viewLine, ref position,
                    session.Generation) &&
                GuideXosText.Append(viewLine, ref position,
                    " caret="u8) &&
                GuideXosText.AppendUnsigned(viewLine, ref position,
                    (uint)_textArea.CaretIndex) &&
                GuideXosText.Append(viewLine, ref position, " anchor="u8) &&
                GuideXosText.AppendUnsigned(viewLine, ref position,
                    (uint)_textArea.AnchorIndex) &&
                GuideXosText.Append(viewLine, ref position, " viewport="u8) &&
                GuideXosText.AppendUnsigned(viewLine, ref position,
                    (uint)_textArea.FirstVisibleLine) &&
                GuideXosText.Append(viewLine, ref position,
                    " history=fresh dirty=false pending=none result=PASS"u8);
            host.TryLog(viewLogged ? viewLine[..position] :
                "C155-RESTORE-VIEW result=FAIL"u8);
            return;
        }

        Span<byte> failure = stackalloc byte[112];
        int failurePosition = 0;
        bool failureLogged = GuideXosText.Append(failure,
                ref failurePosition, "C155-RESTORE generation="u8) &&
            GuideXosText.AppendUnsigned(failure, ref failurePosition,
                session.Generation) &&
            GuideXosText.Append(failure, ref failurePosition,
                " reopen=FAIL fallback=blank pending=none error="u8) &&
            GuideXosText.AppendUnsigned(failure, ref failurePosition,
                unchecked((uint)fileResult)) &&
            GuideXosText.Append(failure, ref failurePosition,
                blankFallbackSafe ? " result=PASS"u8 : " result=FAIL"u8);
        host.TryLog(failureLogged ? failure[..failurePosition] :
            "C155-RESTORE reopen=FAIL fallback=blank result=PASS"u8);
    }

    private void RunC155SessionTests(GuideXosHost host)
    {
        if (s_c155SessionTestsRun) return;
        s_c155SessionTestsRun = true;
        bool passed = GuideXosNotesReturnSessionC155Tests.Run(host);
        if (!passed) host.TryLog("C155-REGRESSIONS result=FAIL"u8);
    }

    private void RunC155RestorationTests(GuideXosHost host)
    {
        if (s_c155RestorationTestsRun) return;
        s_c155RestorationTestsRun = true;

        string startupPath = _currentPath;
        int cases = 0;
        bool all = true;
        all &= RestoreC155TestDocument(host, "/system/apps/C155/alpha.txt",
            73, 61, 3, 167, 73, 61, 3);
        ++cases;
        all &= RestoreC155TestDocument(host, "/system/apps/C155/empty.txt",
            9, 7, 8, 0, 0, 0, 0);
        ++cases;
        all &= RestoreC155TestDocument(host, "/system/apps/C151/max.txt",
            300, 280, 999, 256, 256, 256, 0);
        ++cases;
        all &= RestoreC155TestDocument(host, "/system/apps/C155/alpha.txt",
            999, 998, 999, 167, 167, 167, 7);
        ++cases;
        all &= RestoreC155TestDocument(host, "/system/apps/C155/beta.txt",
            8, 6, 0, 16, 8, 6, 0);
        ++cases;
        all &= RestoreC155Fallback(host,
            "/system/apps/C155/missing.txt", GuideXosFileResult.NotFound);
        ++cases;
        all &= RestoreC155Fallback(host,
            "/system/apps/C151/oversized.txt",
            GuideXosFileResult.BufferTooSmall);
        ++cases;
        all &= RestoreC155Fallback(host, "/system/../invalid.txt",
            GuideXosFileResult.InvalidPath);
        ++cases;
        all &= RestoreC155Fallback(host, "/system/apps/C155",
            GuideXosFileResult.IoFailure);
        ++cases;

        _c152DocumentState.InitializeUntitled(_textArea);
        _currentPath = startupPath;
        UpdatePathLabel();

        bool passed = all && cases == 9;
        host.TryLog(passed
            ? "C155-RESTORE-TEST cases=9 VFS=transactional fallback=blank bounds=PASS result=PASS"u8
            : "C155-RESTORE-TEST result=FAIL"u8);
    }

    private bool RestoreC155TestDocument(GuideXosHost host, string path,
        int caret, int anchor, int viewport, int expectedLength,
        int expectedCaret, int expectedAnchor, int expectedViewport)
    {
        GuideXosNotesReturnSessionDataC155 session = new(true, true, path,
            caret, anchor, viewport, 1u);
        bool restored = TryRestoreC155Session(host, session,
            out GuideXosFileResult fileResult);
        return restored && fileResult == GuideXosFileResult.Success &&
            string.Equals(_currentPath, path, StringComparison.Ordinal) &&
            _textArea.Length == expectedLength && !_c152DocumentState.Dirty &&
            _c152DocumentState.HasSavedRevision &&
            _c152DocumentState.SavedRevision == _textArea.CurrentRevision &&
            _c152DocumentState.SavedRevisionReachable &&
            !_textArea.CanUndo && !_textArea.CanRedo &&
            _textArea.CaretIndex == expectedCaret &&
            _textArea.AnchorIndex == expectedAnchor &&
            _textArea.FirstVisibleLine == expectedViewport &&
            _textArea.FirstVisibleLine <= _textArea.MaximumFirstVisibleLine;
    }

    private bool RestoreC155Fallback(GuideXosHost host, string path,
        GuideXosFileResult expectedResult)
    {
        GuideXosNotesReturnSessionDataC155 session = new(true, true, path,
            4, 2, 1, 1u);
        bool restored = TryRestoreC155Session(host, session,
            out GuideXosFileResult fileResult);
        return !restored && fileResult == expectedResult &&
            string.IsNullOrEmpty(_currentPath) &&
            !_c152DocumentState.HasCurrentPath && !_c152DocumentState.Dirty &&
            _textArea.Length == 0 && !_textArea.CanUndo && !_textArea.CanRedo &&
            _textArea.CaretIndex == 0 && _textArea.AnchorIndex == 0 &&
            _textArea.FirstVisibleLine == 0;
    }
}
#endif
