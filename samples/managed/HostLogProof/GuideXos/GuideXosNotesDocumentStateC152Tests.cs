using System;
using System.Text;

namespace HostLogProof;

public static class GuideXosNotesDocumentStateC152Tests
{
    public static bool Run(GuideXosHost host)
    {
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();
        bool untitledClean = state.InitializeUntitled(area) &&
            !state.HasCurrentPath && !state.Dirty && area.Text.Length == 0 &&
            area.HistoryCount == 1 && !area.CanUndo && !area.CanRedo;

        area.Focus();
        bool untitledDirty = area.HandleCharacter('A') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty &&
            !state.HasCurrentPath && area.CanUndo;
        untitledDirty &= area.Undo() ==
            GuideXosTextAreaEditResult.Changed && area.Text.Length == 0 &&
            !state.Dirty && area.CanRedo && area.Redo() ==
            GuideXosTextAreaEditResult.Changed && state.Dirty &&
            area.Text == "A";

        bool namedClean = state.TryHydrate(area, "/system/apps/C152/alpha.txt",
            "initial"u8) && state.HasCurrentPath && !state.Dirty &&
            area.Text == "initial" && area.HistoryCount == 1 &&
            !area.CanUndo && !area.CanRedo;
        area.Focus();
        bool namedDirty = area.HandleCharacter('!') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty &&
            state.CurrentPath == "/system/apps/C152/alpha.txt";

        int historyBeforeSave = area.HistoryCount;
        bool saveSuccess = state.MarkSaveSucceeded(
            "/system/apps/C152/alpha.txt") && !state.Dirty &&
            state.CurrentPath == "/system/apps/C152/alpha.txt" &&
            area.HistoryCount == historyBeforeSave;
        GuideXosTextRevision savedRevision = state.SavedRevision;
        bool postSaveEdit = area.HandleCharacter('?') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty;
        int historyBeforeFailure = area.HistoryCount;
        GuideXosTextRevision currentBeforeFailure = area.CurrentRevision;
        string priorPath = state.CurrentPath;
        state.MarkSaveFailed();
        bool saveFailure = state.Dirty && state.CurrentPath == priorPath &&
            state.SavedRevision == savedRevision &&
            area.CurrentRevision == currentBeforeFailure &&
            area.HistoryCount == historyBeforeFailure;

        int historyBeforeSaveAs = area.HistoryCount;
        bool saveAsSuccess = state.MarkSaveSucceeded(
            "/system/apps/C152/new.txt") && !state.Dirty &&
            state.CurrentPath == "/system/apps/C152/new.txt" &&
            state.SavedRevision == area.CurrentRevision &&
            area.HistoryCount == historyBeforeSaveAs;
        bool saveAsPathIndependent = area.Undo() ==
                GuideXosTextAreaEditResult.Changed && state.Dirty &&
            state.CurrentPath == "/system/apps/C152/new.txt" &&
            area.Redo() == GuideXosTextAreaEditResult.Changed &&
            !state.Dirty && state.CurrentPath == "/system/apps/C152/new.txt";

        bool emptyFileHydration = state.TryHydrate(
            area, "/system/apps/C152/empty.txt", ReadOnlySpan<byte>.Empty) &&
            area.Text.Length == 0 && !state.Dirty && state.HasCurrentPath &&
            !area.CanUndo && !area.CanRedo;
        int historyBeforeRejectedOpen = area.HistoryCount;
        GuideXosTextRevision revisionBeforeRejectedOpen = area.CurrentRevision;
        bool oversizedRejected = !state.TryHydrate(area,
            "/system/apps/C152/alpha.txt", new byte[257]) &&
            state.CurrentPath == "/system/apps/C152/empty.txt" &&
            area.Text.Length == 0 && !state.Dirty &&
            area.HistoryCount == historyBeforeRejectedOpen &&
            area.CurrentRevision == revisionBeforeRejectedOpen;

        bool emptySave = WriteAndVerify(host, "/system/apps/C152/e00.txt",
            ReadOnlySpan<byte>.Empty);
        byte[] maximumDocument = new byte[256];
        Array.Fill(maximumDocument, (byte)'M');
        bool maximumSave = WriteAndVerify(host,
            "/system/apps/C152/m00.txt", maximumDocument);
        bool saveStress = RunSaveStress(host);
        bool saveAsStress = RunSaveAsStress(host);

        host?.TryLog(emptySave
            ? "C152-EMPTY-SAVE create=read-back=PASS bytes=0 result=PASS"u8
            : "C152-EMPTY-SAVE result=FAIL"u8);
        host?.TryLog(maximumSave
            ? "C152-MAX-SAVE bytes=256 read-back=exact result=PASS"u8
            : "C152-MAX-SAVE result=FAIL"u8);
        host?.TryLog(saveStress
            ? "C152-SAVE-STRESS operations=50 alternating=verified read-back=exact dirty-cleared=true result=PASS"u8
            : "C152-SAVE-STRESS result=FAIL"u8);
        host?.TryLog(saveAsStress
            ? "C152-SAVE-AS-STRESS paths=5 current-path=latest read-back=exact result=PASS"u8
            : "C152-SAVE-AS-STRESS result=FAIL"u8);

        return untitledClean && untitledDirty && namedClean && namedDirty &&
            saveSuccess && saveFailure &&
            saveAsSuccess && saveAsPathIndependent && emptyFileHydration &&
            oversizedRejected && emptySave && maximumSave && saveStress &&
            saveAsStress;
    }

    private static bool RunSaveStress(GuideXosHost host)
    {
        const string path = "/system/apps/C152/s001.txt";
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        byte[] payload = new byte[4];
        byte[] actual = new byte[256];
        Span<byte> expected = stackalloc byte[4];
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();
        if (!state.InitializeUntitled(area)) return false;

        for (int cycle = 0; cycle < 50; cycle++)
        {
            payload[0] = (byte)'S';
            payload[1] = (byte)('0' + cycle / 10);
            payload[2] = (byte)('0' + cycle % 10);
            payload[3] = (byte)(cycle % 2 == 0 ? 'A' : 'B');
            if (!area.SetUtf8(payload.AsSpan(0, 3)) ||
                !state.MarkSaveSucceeded(path)) return false;
            area.Focus();
            if (area.HandleCharacter((char)payload[3]) !=
                    GuideXosTextAreaEditResult.Changed || !state.Dirty ||
                !area.TryCopyUtf8To(expected, out int written) || written != 4)
                return false;
            if (GuideXosFile.WriteAllTextUtf8(host, pathBytes, expected) !=
                GuideXosFileResult.Success ||
                !ReadBackMatches(host, pathBytes, expected, actual) ||
                !state.MarkSaveSucceeded(path) || state.Dirty)
                return false;
        }
        return true;
    }

    private static bool RunSaveAsStress(GuideXosHost host)
    {
        string[] paths =
        [
            "/system/apps/C152/a01.txt",
            "/system/apps/C152/a02.txt",
            "/system/apps/C152/a03.txt",
            "/system/apps/C152/a04.txt",
            "/system/apps/C152/a05.txt",
        ];
        byte[] payload = new byte[8];
        byte[] actual = new byte[256];
        Span<byte> expected = stackalloc byte[8];
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();
        if (!state.InitializeUntitled(area)) return false;

        for (int index = 0; index < paths.Length; index++)
        {
            string path = paths[index];
            byte[] pathBytes = Encoding.UTF8.GetBytes(path);
            payload[0] = (byte)'A';
            payload[1] = (byte)('1' + index);
            payload[2] = (byte)'_';
            payload[3] = (byte)'S';
            payload[4] = (byte)'A';
            payload[5] = (byte)'V';
            payload[6] = (byte)'E';
            payload[7] = (byte)'!';
            if (!area.SetUtf8(payload.AsSpan(0, 7)) ||
                !state.MarkSaveSucceeded("/system/apps/C152/base.txt"))
                return false;
            area.Focus();
            if (area.HandleCharacter((char)payload[7]) !=
                    GuideXosTextAreaEditResult.Changed || !state.Dirty ||
                !area.TryCopyUtf8To(expected, out int written) || written != 8)
                return false;
            if (GuideXosFile.WriteAllTextUtf8(host, pathBytes, expected) !=
                GuideXosFileResult.Success ||
                !ReadBackMatches(host, pathBytes, expected, actual) ||
                !state.MarkSaveSucceeded(path) || state.Dirty ||
                state.CurrentPath != path)
                return false;
        }
        return true;
    }

    private static bool WriteAndVerify(GuideXosHost host, string path,
        ReadOnlySpan<byte> expected)
    {
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        byte[] actual = new byte[256];
        return GuideXosFile.WriteAllTextUtf8(host, pathBytes, expected) ==
                GuideXosFileResult.Success &&
            ReadBackMatches(host, pathBytes, expected, actual);
    }

    private static bool ReadBackMatches(GuideXosHost host,
        ReadOnlySpan<byte> path, ReadOnlySpan<byte> expected, Span<byte> actual)
    {
        GuideXosFileResult stat = GuideXosFile.TryGetInfo(host, path,
            out GuideXosFileInfo info);
        if (stat != GuideXosFileResult.Success || info == null ||
            info.Type != GuideXosEntryType.Regular ||
            info.Size != (ulong)expected.Length)
            return false;
        GuideXosFileResult read = GuideXosFile.ReadAllTextUtf8(host, path,
            actual, out int count);
        return read == GuideXosFileResult.Success && count == expected.Length &&
            actual[..count].SequenceEqual(expected);
    }
}
