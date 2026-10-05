#if HOSTLOGPROOF_C164_FILE_ACTIVATION_PROOF
using System;
using System.Text;

namespace HostLogProof;

internal static class GuideXosNotesActivationC164Tests
{
    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool passed = true;
        GuideXosTextArea area = new(256, 32, 4, 32);
        GuideXosNotesDocumentState state = new();

        passed &= Case(ref cases, state.InitializeUntitled(area) &&
            !state.HasCurrentPath && !state.Dirty && !area.CanUndo &&
            !area.CanRedo);
        passed &= Case(ref cases, state.TryHydrate(area,
            "/system/apps/C164/activation.txt", "A\nB"u8));
        area.SetCaretToStart();
        passed &= Case(ref cases, state.CurrentPath ==
            "/system/apps/C164/activation.txt");
        passed &= Case(ref cases, area.Text == "A\nB" && area.Length == 3);
        passed &= Case(ref cases, !state.Dirty && state.HasSavedRevision &&
            state.SavedRevision == area.CurrentRevision &&
            state.SavedRevisionReachable);
        passed &= Case(ref cases, !area.CanUndo && !area.CanRedo);
        passed &= Case(ref cases, area.CaretIndex == 0 &&
            area.AnchorIndex == 0 && area.FirstVisibleLine == 0);

        if (host != null)
        {
            byte[] helloPath = Encoding.UTF8.GetBytes(
                "/system/apps/C164/hello.txt");
            GuideXosFileResult helloStat = GuideXosFile.TryGetInfo(
                host, helloPath, out GuideXosFileInfo helloInfo);
            Span<byte> vfsBytes = stackalloc byte[256];
            GuideXosFileResult helloRead = GuideXosFile.ReadAllTextUtf8(
                host, helloPath, vfsBytes, out int helloCount);
            passed &= Case(ref cases, helloStat == GuideXosFileResult.Success &&
                helloInfo?.Type == GuideXosEntryType.Regular &&
                helloInfo.Size == 15u);
            passed &= Case(ref cases, helloRead == GuideXosFileResult.Success &&
                vfsBytes[..helloCount].SequenceEqual("hello from C164"u8));

            GuideXosNotesDocumentState vfsState = new();
            GuideXosTextArea vfsArea = new(256, 32, 4, 32);
            passed &= Case(ref cases, vfsState.TryHydrate(vfsArea,
                "/system/apps/C164/hello.txt", vfsBytes[..helloCount]) &&
                !vfsState.Dirty && !vfsArea.CanUndo && !vfsArea.CanRedo);

            byte[] emptyPath = Encoding.UTF8.GetBytes(
                "/system/apps/C151/empty.txt");
            GuideXosFileResult emptyRead = GuideXosFile.ReadAllTextUtf8(
                host, emptyPath, vfsBytes, out int emptyCount);
            passed &= Case(ref cases, emptyRead == GuideXosFileResult.Success &&
                emptyCount == 0 && vfsState.TryHydrate(vfsArea,
                    "/system/apps/C151/empty.txt", vfsBytes[..emptyCount]) &&
                vfsArea.Length == 0 && !vfsState.Dirty);

            byte[] maximumPath = Encoding.UTF8.GetBytes(
                "/system/apps/C151/max.txt");
            GuideXosFileResult maximumRead = GuideXosFile.ReadAllTextUtf8(
                host, maximumPath, vfsBytes, out int maximumCount);
            passed &= Case(ref cases, maximumRead == GuideXosFileResult.Success &&
                maximumCount == 256 && vfsState.TryHydrate(vfsArea,
                    "/system/apps/C151/max.txt", vfsBytes[..maximumCount]) &&
                !vfsState.Dirty && !vfsArea.CanUndo);

            byte[] oversizedPath = Encoding.UTF8.GetBytes(
                "/system/apps/C151/oversized.txt");
            GuideXosFileResult oversizedRead = GuideXosFile.ReadAllTextUtf8(
                host, oversizedPath, vfsBytes, out _);
            passed &= Case(ref cases, oversizedRead ==
                GuideXosFileResult.BufferTooSmall && vfsArea.Length == 256 &&
                vfsState.CurrentPath == "/system/apps/C151/max.txt");

            byte[] missingPath = Encoding.UTF8.GetBytes(
                "/system/apps/C164/missing.txt");
            passed &= Case(ref cases, GuideXosFile.TryGetInfo(host,
                    missingPath, out _) == GuideXosFileResult.NotFound &&
                GuideXosFile.ReadAllTextUtf8(host, missingPath, vfsBytes,
                    out _) == GuideXosFileResult.NotFound);

            byte[] directoryPath = Encoding.UTF8.GetBytes(
                "/system/apps/C164");
            passed &= Case(ref cases, GuideXosFile.TryGetInfo(host,
                    directoryPath, out GuideXosFileInfo directoryInfo) ==
                GuideXosFileResult.Success && directoryInfo?.Type ==
                    GuideXosEntryType.Directory);

            byte[] tooManyLinesPath = Encoding.UTF8.GetBytes(
                "/system/apps/C164/lines.txt");
            GuideXosFileResult linesRead = GuideXosFile.ReadAllTextUtf8(
                host, tooManyLinesPath, vfsBytes, out int linesCount);
            string priorVfsPath = vfsState.CurrentPath;
            string priorVfsText = vfsArea.Text;
            passed &= Case(ref cases, linesRead == GuideXosFileResult.Success &&
                !vfsState.TryHydrate(vfsArea, "/system/apps/C164/lines.txt",
                    vfsBytes[..linesCount]) &&
                vfsState.CurrentPath == priorVfsPath &&
                vfsArea.Text == priorVfsText);

            byte[] invalidPath = Encoding.UTF8.GetBytes(
                "/system/apps/C164/invalid.txt");
            GuideXosFileResult invalidRead = GuideXosFile.ReadAllTextUtf8(
                host, invalidPath, vfsBytes, out int invalidCount);
            passed &= Case(ref cases, invalidRead == GuideXosFileResult.Success &&
                !vfsState.TryHydrate(vfsArea, "/system/apps/C164/invalid.txt",
                    vfsBytes[..invalidCount]) &&
                vfsState.CurrentPath == priorVfsPath &&
                vfsArea.Text == priorVfsText);
        }

        area.Focus();
        passed &= Case(ref cases, area.HandleCharacter('X') ==
            GuideXosTextAreaEditResult.Changed && state.Dirty && area.CanUndo);
        passed &= Case(ref cases, area.Undo() ==
            GuideXosTextAreaEditResult.Changed && !state.Dirty &&
            area.CanRedo && area.Text == "A\nB");
        passed &= Case(ref cases, area.Redo() ==
            GuideXosTextAreaEditResult.Changed && state.Dirty &&
            area.CanUndo && area.Text == "XA\nB");
        passed &= Case(ref cases, area.Undo() ==
            GuideXosTextAreaEditResult.Changed && !state.Dirty);
        passed &= Case(ref cases, state.MarkSaveSucceeded(
            "/system/apps/C164/activation.txt") && !state.Dirty &&
            state.CurrentPath == "/system/apps/C164/activation.txt");

        string stablePath = state.CurrentPath;
        string stableText = area.Text;
        GuideXosTextRevision stableRevision = area.CurrentRevision;
        passed &= Case(ref cases, !state.TryHydrate(area,
            "/system/apps/../bad.txt", "bad"u8));
        passed &= Case(ref cases, state.CurrentPath == stablePath &&
            area.Text == stableText && area.CurrentRevision == stableRevision &&
            !state.Dirty);
        passed &= Case(ref cases, !state.TryHydrate(area,
            "/system/apps/C164/large.txt", new byte[257]));
        passed &= Case(ref cases, state.CurrentPath == stablePath &&
            area.Text == stableText && area.CurrentRevision == stableRevision);
        Span<byte> tooManyLines = stackalloc byte[33];
        tooManyLines.Fill((byte)'\n');
        passed &= Case(ref cases, !state.TryHydrate(area,
            "/system/apps/C164/lines.txt", tooManyLines));
        passed &= Case(ref cases, state.CurrentPath == stablePath &&
            area.Text == stableText && area.CurrentRevision == stableRevision);
        Span<byte> invalidUtf8 = stackalloc byte[] { 0xC2 };
        passed &= Case(ref cases, !state.TryHydrate(area,
            "/system/apps/C164/invalid.txt", invalidUtf8));
        passed &= Case(ref cases, state.CurrentPath == stablePath &&
            area.Text == stableText && area.CurrentRevision == stableRevision);

        passed &= Case(ref cases, state.InitializeUntitled(area) &&
            !state.HasCurrentPath && !state.Dirty && area.Text.Length == 0 &&
            !area.CanUndo && !area.CanRedo && area.CaretIndex == 0 &&
            area.AnchorIndex == 0 && area.FirstVisibleLine == 0);
        bool repeatedBaseline = true;
        for (int cycle = 0; cycle < 25; cycle++)
        {
            string path = "/system/apps/C164/cycle" + cycle + ".txt";
            repeatedBaseline &= state.TryHydrate(area, path, "cycle"u8) &&
                state.CurrentPath == path && !state.Dirty && !area.CanUndo &&
                !area.CanRedo && state.SavedRevision == area.CurrentRevision;
        }
        passed &= Case(ref cases, repeatedBaseline);

        if (host != null)
        {
            Span<byte> line = stackalloc byte[112];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C164-NOTES-STATE-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)cases);
            GuideXosText.Append(line, ref position,
                passed && cases >= 20
                    ? " activation-clean=PASS dirty-undo-redo=PASS stress=25 result=PASS"u8
                    : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return passed && cases >= 20;
    }

    private static bool Case(ref int cases, bool result)
    {
        cases++;
        return result;
    }
}
#endif
