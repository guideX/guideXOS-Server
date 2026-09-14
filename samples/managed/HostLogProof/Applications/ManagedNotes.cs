using System;

namespace HostLogProof.Applications;

/// <summary>
/// Small proof application for the reusable managed file service. It is
/// intentionally a bounded action flow rather than a general text editor:
/// New note -> Append -> Save -> Reload.
/// </summary>
public sealed class ManagedNotes : GuideXosApplication
{
    private static readonly byte[] s_path = "/system/apps/NOTES.TXT"u8.ToArray();
    private static readonly byte[] s_initial = "Hello from Managed Notes"u8.ToArray();
    private static byte[] s_note = s_initial;
    private static byte[] s_status = "New note"u8.ToArray();
    private static uint s_launchCount;
    [ThreadStatic]
    private static uint s_threadLaunchCount;
    private static ulong s_window;
    private static uint s_nextAction;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        uint launchCount = ++s_launchCount;
        uint threadLaunchCount = ++s_threadLaunchCount;
        if (threadLaunchCount != launchCount) return GuideXosResult.InvalidArgument;
        GuideXosFileResult fileResult = GuideXosFile.ReadAllTextUtf8(
            host, s_path, out byte[] loaded);
        if (fileResult == GuideXosFileResult.NotFound)
        {
            s_note = s_initial.AsSpan().ToArray();
            s_status = "New note"u8.ToArray();
            s_nextAction = 1u;
        }
        else if (fileResult == GuideXosFileResult.Success)
        {
            s_note = loaded;
            s_status = "Loaded from VFS"u8.ToArray();
            s_nextAction = 3u;
        }
        else
        {
            s_note = Array.Empty<byte>();
            s_status = StatusText(fileResult);
            s_nextAction = 3u;
        }

        GuideXosResult result = host.TryCreateSurface(
            "Managed Notes"u8, 520, 300, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        s_window = surface.Handle;
        if (!Render(host, surface, launchCount)) return GuideXosResult.InvalidArgument;
        if (host.TryLog("C113-NOTES threadStatic=PASS"u8) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }

        if (host.IsCapabilityProbe && !host.HasCapability(GuideXosCapability.FileWrite))
        {
            GuideXosFileResult blocked = GuideXosFile.WriteAllTextUtf8(
                host, s_path, s_note);
            bool safeDowngrade = blocked == GuideXosFileResult.CapabilityUnavailable;
            s_status = safeDowngrade
                ? "Write unavailable"u8.ToArray()
                : "Unexpected write result"u8.ToArray();
            Render(host, surface, launchCount);
            host.TryLog(safeDowngrade
                ? "C113-CAPABILITY-DOWNGRADE fileWrite=omitted save=REJECTED result=PASS"u8
                : "C113-CAPABILITY-DOWNGRADE fileWrite=omitted save=UNEXPECTED result=FAIL"u8);
            return safeDowngrade ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }

        return host.TryLog(fileResult == GuideXosFileResult.NotFound
            ? "C113-NOTES launch=PASS file=NOT_FOUND state=NEW"u8
            : fileResult == GuideXosFileResult.Success
                ? "C113-NOTES launch=PASS file=LOADED source=VFS"u8
                : "C113-NOTES launch=PASS file=READ_ERROR"u8) == GuideXosResult.Success
            ? GuideXosResult.Success
            : GuideXosResult.InvalidArgument;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (host.TryGetSurface(s_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }

        if (actionId == 1u && s_nextAction == 1u)
        {
            byte[] edited = new byte[s_note.Length + 9];
            for (int index = 0; index < s_note.Length; index++) edited[index] = s_note[index];
            " [edited]"u8.CopyTo(edited.AsSpan(s_note.Length));
            s_note = edited;
            s_status = "Edited in managed code"u8.ToArray();
            s_nextAction = 2u;
            if (!Render(host, surface, s_launchCount) ||
                host.TryLog("C113-NOTES action=append result=PASS"u8) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }

        if (actionId == 2u && s_nextAction == 2u)
        {
            GuideXosFileResult writeResult = GuideXosFile.WriteAllTextUtf8(
                host, s_path, s_note);
            if (writeResult != GuideXosFileResult.Success)
            {
                s_status = StatusText(writeResult);
                Render(host, surface, s_launchCount);
                host.TryLog("C113-NOTES action=save result=REJECTED"u8);
                return writeResult == GuideXosFileResult.CapabilityUnavailable
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }

            s_status = "Saved to VFS"u8.ToArray();
            s_nextAction = 3u;
            if (!Render(host, surface, s_launchCount) ||
                host.TryLog("C113-NOTES action=save result=PASS"u8) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }

        if (actionId == 3u && s_nextAction == 3u)
        {
            GuideXosFileResult reloadResult = GuideXosFile.ReadAllTextUtf8(
                host, s_path, out byte[] reloaded);
            if (reloadResult == GuideXosFileResult.Success)
            {
                s_note = reloaded;
                s_status = "Reloaded from VFS"u8.ToArray();
            }
            else
            {
                s_status = StatusText(reloadResult);
            }
            if (!Render(host, surface, s_launchCount) ||
                host.TryLog(reloadResult == GuideXosFileResult.Success
                    ? "C113-NOTES action=reload result=PASS source=VFS"u8
                    : "C113-NOTES action=reload result=FAIL"u8) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
            return reloadResult == GuideXosFileResult.Success
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }

        return GuideXosResult.InvalidAction;
    }

    private static bool Render(GuideXosHost host, GuideXosSurface surface, uint launchCount)
    {
        return surface.TryFillRect(10, 10, 500, 230, 0x007A5A9Au) == GuideXosResult.Success &&
            GuideXosText.Line(surface, 24, "Managed Notes | "u8, "file service"u8) &&
            GuideXosText.CountLine(surface, 52, "Launches: "u8, launchCount) &&
            GuideXosText.Line(surface, 80, "Path: "u8, "/system/apps/NOTES.TXT"u8) &&
            NoteLine(surface, 108) &&
            GuideXosText.Line(surface, 136, "Status: "u8, s_status) &&
            GuideXosText.Line(surface, 164, "Source: "u8, "bounded VFS UTF-8"u8) &&
            AddNextButton(host, surface);
    }

    private static bool AddNextButton(GuideXosHost host, GuideXosSurface surface)
    {
        if (s_nextAction == 0u) return true;
        ReadOnlySpan<byte> label = s_nextAction switch
        {
            1u => "Append [edited]"u8,
            2u => "Save note"u8,
            _ => "Reload note"u8,
        };
        return surface.TryAddButton(20, 200, 180, 28, label, s_nextAction, out _) ==
            GuideXosResult.Success;
    }

    private static bool NoteLine(GuideXosSurface surface, int y)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, "Note: "u8)) return false;
        int remaining = line.Length - position;
        int copyLength = s_note.Length < remaining ? s_note.Length : remaining;
        for (int index = 0; index < copyLength; index++) line[position++] = s_note[index];
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    private static byte[] StatusText(GuideXosFileResult result)
    {
        return result switch
        {
            GuideXosFileResult.InvalidPath => "Invalid path"u8.ToArray(),
            GuideXosFileResult.FileTooLarge => "File too large"u8.ToArray(),
            GuideXosFileResult.BufferTooSmall => "Buffer too small"u8.ToArray(),
            GuideXosFileResult.CapabilityUnavailable => "File access unavailable"u8.ToArray(),
            _ => "File I/O failure"u8.ToArray(),
        };
    }
}
