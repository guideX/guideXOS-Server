using System;
using System.Text;

namespace HostLogProof.Applications;

/// <summary>
/// Small proof application for the reusable managed file service. It is
/// intentionally a bounded action flow rather than a general text editor:
/// New note -> Append -> Save -> Reload.
/// </summary>
public sealed class ManagedNotes : GuideXosApplication
{
#if !HOSTLOGPROOF_C115_MANAGED_FILE_PICKER
    private static readonly byte[] s_path = "/system/apps/NOTES.TXT"u8.ToArray();
    private static readonly byte[] s_initial = "Hello from Managed Notes"u8.ToArray();
    private static byte[] s_note = s_initial;
    private static byte[] s_status = "New note"u8.ToArray();
    private static uint s_launchCount;
    [ThreadStatic]
    private static uint s_threadLaunchCount;
    private static ulong s_window;
    private static uint s_nextAction;
    private static bool s_browserMode;
    private static GuideXosDirectorySnapshot s_directory;
    private static GuideXosDirectoryEntry s_selectedEntry;
    private static int s_selectedIndex;
    private static byte[] s_browserStatus = "Directory ready"u8.ToArray();

    public override GuideXosResult Launch(GuideXosHost host)
    {
        s_browserMode = IsBrowserContext(host);
        return s_browserMode ? LaunchBrowser(host) : LaunchLegacy(host);
    }

    private static bool IsBrowserContext(GuideXosHost host)
    {
        ReadOnlySpan<byte> context = host.LaunchContext.Utf8;
        ReadOnlySpan<byte> prefix = "c114-notes"u8;
        if (context.Length < prefix.Length) return false;
        for (int index = 0; index < prefix.Length; index++)
        {
            if (context[index] != prefix[index]) return false;
        }
        return true;
    }

    private static GuideXosResult LaunchLegacy(GuideXosHost host)
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
        if (s_browserMode) return HandleBrowserAction(host, actionId);
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

    private static GuideXosResult LaunchBrowser(GuideXosHost host)
    {
        GuideXosFileResult listResult = GuideXosFile.TryListDirectory(
            host, "/system/apps"u8, out GuideXosDirectorySnapshot listing);
        if (listResult != GuideXosFileResult.Success || listing == null)
        {
            s_browserStatus = StatusText(listResult);
            s_directory = null;
            s_selectedEntry = null;
        }
        else
        {
            s_directory = listing;
            s_selectedIndex = FindFirstTextFile(listing);
            s_selectedEntry = s_selectedIndex >= 0 ? listing.Entries[s_selectedIndex] : null;
            s_browserStatus = listing.HasMore
                ? "Directory truncated"u8.ToArray() : "Directory ready"u8.ToArray();
            host.TryLog("C114-NOTES list=PASS source=VFS"u8);
        }
        GuideXosResult result = host.TryCreateSurface(
            "Managed Notes"u8, 520, 300, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        s_window = surface.Handle;
        if (!RenderBrowser(host, surface)) return GuideXosResult.InvalidArgument;
        host.TryLog("C114-NOTES threadStatic=PASS"u8);
        return GuideXosResult.Success;
    }

    private static int FindFirstTextFile(GuideXosDirectorySnapshot listing)
    {
        for (int index = 0; index < listing.Entries.Length; index++)
        {
            GuideXosDirectoryEntry entry = listing.Entries[index];
            if (entry.Type == GuideXosEntryType.Regular &&
                entry.Name.EndsWith(".TXT", StringComparison.OrdinalIgnoreCase))
            {
                return index;
            }
        }
        return listing.Entries.Length == 0 ? -1 : 0;
    }

    private static GuideXosResult HandleBrowserAction(GuideXosHost host, uint actionId)
    {
        if (host.TryGetSurface(s_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        if (actionId == 10u && s_selectedEntry != null)
        {
            byte[] path = PathForEntry(s_selectedEntry.Name);
            GuideXosFileResult statResult = GuideXosFile.TryGetInfo(host, path, out GuideXosFileInfo info);
            if (statResult != GuideXosFileResult.Success || info == null ||
                info.Type != GuideXosEntryType.Regular)
            {
                s_browserStatus = StatusText(statResult);
                RenderBrowser(host, surface);
                return GuideXosResult.Success;
            }
            GuideXosFileResult readResult = GuideXosFile.ReadAllTextUtf8(host, path, out byte[] loaded);
            if (readResult != GuideXosFileResult.Success)
            {
                s_browserStatus = StatusText(readResult);
                RenderBrowser(host, surface);
                return GuideXosResult.Success;
            }
            s_note = loaded;
            s_status = "Opened from VFS"u8.ToArray();
            s_browserStatus = "Opened selected document"u8.ToArray();
            host.TryLog("C114-NOTES open=PASS source=directory-list"u8);
            if (!RenderBrowser(host, surface)) return GuideXosResult.InvalidArgument;
            return GuideXosResult.Success;
        }
        if (actionId == 11u && s_directory != null && s_directory.Entries.Length != 0)
        {
            s_selectedIndex = (s_selectedIndex + 1) % s_directory.Entries.Length;
            s_selectedEntry = s_directory.Entries[s_selectedIndex];
            s_browserStatus = "Selection advanced"u8.ToArray();
            return RenderBrowser(host, surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 12u)
        {
            s_browserStatus = "Back to directory list"u8.ToArray();
            return RenderBrowser(host, surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 13u && s_selectedEntry != null)
        {
            byte[] edited = new byte[s_note.Length + 9];
            s_note.CopyTo(edited, 0);
            " [edited]"u8.CopyTo(edited.AsSpan(s_note.Length));
            s_note = edited;
            s_browserStatus = "Edited in managed code"u8.ToArray();
            host.TryLog("C114-NOTES action=edit result=PASS"u8);
            return RenderBrowser(host, surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 14u && s_selectedEntry != null)
        {
            GuideXosFileResult saveResult = GuideXosFile.WriteAllTextUtf8(
                host, PathForEntry(s_selectedEntry.Name), s_note);
            s_browserStatus = saveResult == GuideXosFileResult.Success
                ? "Saved to VFS"u8.ToArray() : StatusText(saveResult);
            host.TryLog(saveResult == GuideXosFileResult.Success
                ? "C114-NOTES action=save result=PASS"u8
                : "C114-NOTES action=save result=FAIL"u8);
            return RenderBrowser(host, surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 15u && s_selectedEntry != null)
        {
            GuideXosFileResult reloadResult = GuideXosFile.ReadAllTextUtf8(
                host, PathForEntry(s_selectedEntry.Name), out byte[] reloaded);
            if (reloadResult == GuideXosFileResult.Success) s_note = reloaded;
            s_browserStatus = reloadResult == GuideXosFileResult.Success
                ? "Reloaded from VFS"u8.ToArray() : StatusText(reloadResult);
            host.TryLog(reloadResult == GuideXosFileResult.Success
                ? "C114-NOTES action=reload result=PASS"u8
                : "C114-NOTES action=reload result=FAIL"u8);
            return RenderBrowser(host, surface) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.InvalidAction;
    }

    private static byte[] PathForEntry(string name)
    {
        return Encoding.UTF8.GetBytes("/system/apps/" + name);
    }

    private static bool RenderBrowser(GuideXosHost host, GuideXosSurface surface)
    {
        if (surface.TryFillRect(10, 10, 500, 230, 0x007A5A9Au) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed Notes | "u8, "directory browser"u8) ||
            !GuideXosText.Line(surface, 52, "Directory: "u8, "/system/apps"u8)) return false;
        if (s_directory != null)
        {
            int y = 80;
            int displayed = 0;
            for (int index = 0; index < s_directory.Entries.Length && displayed < 3; index++)
            {
                GuideXosDirectoryEntry entry = s_directory.Entries[index];
                byte[] name = Encoding.UTF8.GetBytes(entry.Name);
                Span<byte> line = stackalloc byte[64];
                int position = 0;
                if (!GuideXosText.Append(line, ref position,
                        index == s_selectedIndex ? "> "u8 : "  "u8) ||
                    !GuideXosText.Append(line, ref position, name) ||
                    !GuideXosText.Append(line, ref position, entry.Type == GuideXosEntryType.Directory
                        ? " [DIR]"u8 : " [FILE]"u8) ||
                    surface.TrySetText(20, y, line[..position]) != GuideXosResult.Success) return false;
                y += 24;
                displayed++;
            }
        }
        if (!RenderContentPreview(surface, 136)) return false;
        if (!GuideXosText.Line(surface, 160, "Status: "u8, s_browserStatus))
        {
            return false;
        }
        ReadOnlySpan<byte> selectedLabel = "<none>"u8;
        byte[] selectedName = null;
        if (s_selectedEntry != null)
        {
            selectedName = Encoding.UTF8.GetBytes(s_selectedEntry.Name);
            selectedLabel = selectedName;
        }
        if (!RenderSelectedInfo(surface, selectedLabel) ||
            surface.TryAddButton(20, 210, 100, 28, "Open"u8, 10u, out _) != GuideXosResult.Success)
        {
            return false;
        }
        return surface.TryAddButton(130, 210, 100, 28, "Next"u8, 11u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(240, 210, 100, 28, "Edit"u8, 13u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(350, 210, 100, 28, "Save"u8, 14u, out _) == GuideXosResult.Success;
    }

    private static bool RenderContentPreview(GuideXosSurface surface, int y)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, "Content: "u8)) return false;
        int length = Math.Min(s_note.Length, line.Length - position);
        if (length != 0 && !GuideXosText.Append(line, ref position, s_note.AsSpan(0, length))) return false;
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    private static bool RenderSelectedInfo(
        GuideXosSurface surface, ReadOnlySpan<byte> selectedLabel)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, "Selected: "u8) ||
            !GuideXosText.Append(line, ref position, selectedLabel) ||
            !GuideXosText.Append(line, ref position, " ("u8)) return false;
        ReadOnlySpan<byte> type = s_selectedEntry == null ||
            s_selectedEntry.Type == GuideXosEntryType.Regular ? "FILE"u8 : "DIR"u8;
        if (!GuideXosText.Append(line, ref position, type) ||
            !GuideXosText.Append(line, ref position, ", size="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position,
                (uint)(s_selectedEntry?.Size ?? 0u)) ||
            !GuideXosText.Append(line, ref position, ")"u8)) return false;
        return surface.TrySetText(20, 184, line[..position]) == GuideXosResult.Success;
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
            GuideXosFileResult.NotFound => "Not found"u8.ToArray(),
            GuideXosFileResult.NotDirectory => "Not a directory"u8.ToArray(),
            GuideXosFileResult.EntryNameTooLong => "Entry name too long"u8.ToArray(),
            GuideXosFileResult.FileTooLarge => "File too large"u8.ToArray(),
            GuideXosFileResult.BufferTooSmall => "Buffer too small"u8.ToArray(),
            GuideXosFileResult.CapabilityUnavailable => "File access unavailable"u8.ToArray(),
            _ => "File I/O failure"u8.ToArray(),
        };
    }
#else
    private readonly GuideXosFilePicker _picker = new();
    private readonly byte[] _initial = "Hello from Managed Notes"u8.ToArray();
    private byte[] _note = "Hello from Managed Notes"u8.ToArray();
    private byte[] _status = "Ready"u8.ToArray();
    private string _currentPath = "/system/apps/NOTES.TXT";
    private ulong _window;
    private uint _launchCount;
    private uint _saveInvocation;
    [ThreadStatic]
    private static uint s_threadLaunchCount;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return RunCapabilityProbe(host);
        uint launchCount = ++_launchCount;
        uint threadLaunchCount = ++s_threadLaunchCount;
        if (threadLaunchCount != launchCount)
        {
            host.TryLog("C115-NOTES threadStatic=FAIL"u8);
            return GuideXosResult.InvalidArgument;
        }
        _picker.Reset();
        _currentPath = "/system/apps/NOTES.TXT";
        _saveInvocation = 0u;
        GuideXosFileResult loadResult = GuideXosFile.ReadAllTextUtf8(
            host, Encoding.UTF8.GetBytes(_currentPath), out byte[] loaded);
        if (loadResult == GuideXosFileResult.Success)
        {
            _note = loaded;
            _status = "Loaded from VFS"u8.ToArray();
        }
        else if (loadResult == GuideXosFileResult.NotFound)
        {
            _note = _initial.AsSpan().ToArray();
            _status = "New note"u8.ToArray();
        }
        else
        {
            _note = Array.Empty<byte>();
            _status = StatusText(loadResult);
        }
        GuideXosResult result = host.TryCreateSurface(
            "Managed Notes"u8, 520, 300, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        _window = surface.Handle;
        if (!RenderMain(host, surface, launchCount)) return GuideXosResult.InvalidArgument;
        if (host.TryLog("C115-NOTES threadStatic=PASS"u8) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        if (IsContext(host, "c115-notes-nomatch"u8))
        {
            GuideXosFilePickerResult pickerResult = _picker.OpenFile(
                host, surface, GuideXosFilePickerOptions.Open(
                    "/system/apps", "Open document", ".ZZZ"));
            host.TryLog(pickerResult.Status == GuideXosFilePickerStatus.NoMatchingFiles
                ? "C115-NOTES no-match=PASS"u8
                : "C115-NOTES no-match=FAIL"u8);
            return pickerResult.Status == GuideXosFilePickerStatus.NoMatchingFiles
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (IsContext(host, "c115-notes-overwrite"u8))
        {
            return BeginSave(host, surface, "NOTES.TXT") ==
                GuideXosFilePickerStatus.Pending
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (IsContext(host, "c115-notes-negative"u8))
        {
            return RunNegativeProbe(host);
        }
        return GuideXosResult.Success;
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        if (_picker.IsActive)
        {
            return HandlePickerAction(host, surface, actionId);
        }
        if (actionId == 20u)
        {
            GuideXosFilePickerResult result = _picker.OpenFile(
                host, surface, GuideXosFilePickerOptions.Open(
                    "/system/apps", "Open document", ".TXT"));
            return PickerStartResult(result);
        }
        if (actionId == 21u)
        {
            host.TryLog("C115-PICKER save=begin"u8);
            GuideXosFilePickerStatus status = BeginSave(
                host, surface, _saveInvocation++ == 0u ? "THIRD.TXT" : "NOTES.TXT");
            host.TryLog(status == GuideXosFilePickerStatus.Pending
                ? "C115-PICKER save=pending"u8
                : "C115-PICKER save=unexpected"u8);
            return status == GuideXosFilePickerStatus.Pending
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 1u)
        {
            byte[] edited = new byte[_note.Length + 9];
            _note.CopyTo(edited, 0);
            " [edited]"u8.CopyTo(edited.AsSpan(_note.Length));
            _note = edited;
            _status = "Edited in managed code"u8.ToArray();
            if (!RenderMain(host, surface, _launchCount) ||
                host.TryLog("C115-NOTES action=edit result=PASS"u8) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }
        if (actionId == 3u)
        {
            GuideXosFileResult reloadResult = GuideXosFile.ReadAllTextUtf8(
                host, Encoding.UTF8.GetBytes(_currentPath), out byte[] reloaded);
            if (reloadResult == GuideXosFileResult.Success)
            {
                _note = reloaded;
                _status = "Reloaded from VFS"u8.ToArray();
            }
            else
            {
                _status = StatusText(reloadResult);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.InvalidAction;
    }

    private GuideXosResult HandlePickerAction(
        GuideXosHost host, GuideXosSurface surface, uint actionId)
    {
        GuideXosFilePickerResult result = _picker.HandleAction(host, surface, actionId);
        if (result.Status == GuideXosFilePickerStatus.Selected)
        {
            if (_picker.Mode == GuideXosFilePickerMode.Open)
            {
                GuideXosFileResult readResult = GuideXosFile.ReadAllTextUtf8(
                    host, Encoding.UTF8.GetBytes(result.Path), out byte[] loaded);
                if (readResult != GuideXosFileResult.Success)
                {
                    _status = StatusText(readResult);
                    return RenderMain(host, surface, _launchCount)
                        ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                }
                _currentPath = result.Path;
                _note = loaded;
                _status = "Opened from picker"u8.ToArray();
                host.TryLog(PathLog("C115-NOTES open=PASS path=", result.Path));
            }
            else
            {
                GuideXosFileResult writeResult = GuideXosFile.WriteAllTextUtf8(
                    host, Encoding.UTF8.GetBytes(result.Path), _note);
                if (writeResult != GuideXosFileResult.Success)
                {
                    _status = StatusText(writeResult);
                    host.TryLog("C115-NOTES save=REJECTED"u8);
                    return RenderMain(host, surface, _launchCount)
                        ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                }
                _currentPath = result.Path;
                _status = "Saved through picker"u8.ToArray();
                host.TryLog(PathLog("C115-NOTES save=PASS path=", result.Path));
            }
            _picker.Reset();
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (result.Status == GuideXosFilePickerStatus.Cancelled)
        {
            _status = _picker.Mode == GuideXosFilePickerMode.Save
                ? "Save cancelled"u8.ToArray() : "Open cancelled"u8.ToArray();
            _picker.Reset();
            host.TryLog(_status.AsSpan().SequenceEqual("Save cancelled"u8)
                ? "C115-NOTES save=cancelled result=PASS"u8
                : "C115-NOTES open=cancelled result=PASS"u8);
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (result.Status == GuideXosFilePickerStatus.OverwriteDeclined)
        {
            host.TryLog("C115-NOTES overwrite=declined result=PASS"u8);
        }
        return GuideXosResult.Success;
    }

    private GuideXosFilePickerStatus BeginSave(
        GuideXosHost host, GuideXosSurface surface, string name)
    {
        GuideXosFilePickerResult result = _picker.SaveFile(
            host, surface, GuideXosFilePickerOptions.Save(
                "/system/apps", "Save document", ".TXT", name, true));
        return result.Status;
    }

    private static GuideXosResult PickerStartResult(GuideXosFilePickerResult result)
    {
        return result.Status == GuideXosFilePickerStatus.Pending ||
            result.Status == GuideXosFilePickerStatus.NoMatchingFiles
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private bool RenderMain(GuideXosHost host, GuideXosSurface surface, uint launchCount)
    {
        return surface.TryFillRect(10, 10, 500, 230, 0x007A5A9Au) == GuideXosResult.Success &&
            GuideXosText.Line(surface, 24, "Managed Notes | "u8, "Open / Save"u8) &&
            GuideXosText.CountLine(surface, 48, "Launches: "u8, launchCount) &&
            GuideXosText.Line(surface, 72, "Path: "u8, Encoding.UTF8.GetBytes(_currentPath)) &&
            NoteLine(surface, 100) &&
            GuideXosText.Line(surface, 128, "Status: "u8, _status) &&
            GuideXosText.Line(surface, 156, "Filter: "u8, ".TXT (case-insensitive)"u8) &&
            surface.TryAddButton(20, 200, 90, 28, "Open"u8, 20u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(120, 200, 100, 28, "Save As"u8, 21u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(230, 200, 90, 28, "Edit"u8, 1u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(330, 200, 100, 28, "Reload"u8, 3u, out _) == GuideXosResult.Success;
    }

    private bool NoteLine(GuideXosSurface surface, int y)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, "Content: "u8)) return false;
        int length = Math.Min(_note.Length, line.Length - position);
        for (int index = 0; index < length; index++) line[position++] = _note[index];
        return surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    private static bool IsContext(GuideXosHost host, ReadOnlySpan<byte> expected)
    {
        return host.LaunchContext.Utf8.SequenceEqual(expected);
    }

    private static byte[] PathLog(string prefix, string path)
    {
        byte[] prefixBytes = Encoding.UTF8.GetBytes(prefix);
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        byte[] result = new byte[Math.Min(127, prefixBytes.Length + pathBytes.Length)];
        int length = Math.Min(prefixBytes.Length, result.Length);
        prefixBytes.AsSpan(0, length).CopyTo(result);
        int remaining = result.Length - length;
        if (remaining > 0) pathBytes.AsSpan(0, Math.Min(remaining, pathBytes.Length))
            .CopyTo(result.AsSpan(length));
        return result;
    }

    private static GuideXosResult RunCapabilityProbe(GuideXosHost host)
    {
        bool passed = true;
        if (!host.HasCapability(GuideXosCapability.DirectoryList))
        {
            passed &= GuideXosFile.TryListDirectory(
                host, "/system/apps"u8, out _) == GuideXosFileResult.CapabilityUnavailable;
            host.TryLog(passed
                ? "C115-CAPABILITY-DOWNGRADE directoryList=omitted picker=CapabilityUnavailable result=PASS"u8
                : "C115-CAPABILITY-DOWNGRADE directoryList=omitted result=FAIL"u8);
        }
        if (!host.HasCapability(GuideXosCapability.FileStat))
        {
            passed &= GuideXosFile.TryGetInfo(
                host, "/system/apps"u8, out _) == GuideXosFileResult.CapabilityUnavailable;
            host.TryLog(passed
                ? "C115-CAPABILITY-DOWNGRADE fileStat=omitted picker=CapabilityUnavailable result=PASS"u8
                : "C115-CAPABILITY-DOWNGRADE fileStat=omitted result=FAIL"u8);
        }
        if (!host.HasCapability(GuideXosCapability.FileWrite))
        {
            passed &= GuideXosFile.WriteAllTextUtf8(
                host, "/system/apps/NOTES.TXT"u8, "probe"u8) ==
                GuideXosFileResult.CapabilityUnavailable;
            host.TryLog(passed
                ? "C115-CAPABILITY-DOWNGRADE fileWrite=omitted app=rejects-save result=PASS"u8
                : "C115-CAPABILITY-DOWNGRADE fileWrite=omitted result=FAIL"u8);
        }
        return passed ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private static GuideXosResult RunNegativeProbe(GuideXosHost host)
    {
        GuideXosPickerPathStatus invalidDirectory =
            GuideXosPickerPath.TryNormalizeDirectory(
                "/system/apps/../", out _);
        GuideXosPickerPathStatus invalidFilename =
            GuideXosPickerPath.TryBuildPath(
                "/system/apps", "../BAD.TXT", out _);
        GuideXosPickerPathStatus overflow =
            GuideXosPickerPath.TryBuildPath(
                "/system/apps/" + new string('A', 80), "A.TXT", out _);
        bool passed = invalidDirectory == GuideXosPickerPathStatus.InvalidDirectory &&
            invalidFilename == GuideXosPickerPathStatus.InvalidFilename &&
            overflow == GuideXosPickerPathStatus.PathTooLong &&
            !GuideXosFilePicker.IsSelectableOpenType(GuideXosEntryType.Directory) &&
            !GuideXosFilePicker.IsValidSelectionIndex(-1, 0);
        host.TryLog(passed
            ? "C115-NEGATIVE directory=PASS filename=PASS overflow=PASS dirType=PASS index=PASS result=PASS"u8
            : "C115-NEGATIVE result=FAIL"u8);
        return passed ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private static byte[] StatusText(GuideXosFileResult result)
    {
        return result switch
        {
            GuideXosFileResult.InvalidPath => "Invalid path"u8.ToArray(),
            GuideXosFileResult.NotFound => "Not found"u8.ToArray(),
            GuideXosFileResult.NotDirectory => "Not a directory"u8.ToArray(),
            GuideXosFileResult.FileTooLarge => "File too large"u8.ToArray(),
            GuideXosFileResult.CapabilityUnavailable => "File access unavailable"u8.ToArray(),
            _ => "File I/O failure"u8.ToArray(),
        };
    }
#endif
}
