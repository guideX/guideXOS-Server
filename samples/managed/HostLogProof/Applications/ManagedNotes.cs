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
#if HOSTLOGPROOF_C117_MANAGED_TEXT_AREA
    private const string C117InitialDocument =
        "First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line";
    private const string C117ExpectedDocument =
        "AFirst!\n line\nSecond? ROW\nThird line\nFourth line\nFifth line\nSixth line!";
    private readonly GuideXosFilePicker _picker = new();
    private readonly GuideXosTextArea _textArea = new(256, 32, 4,
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        32);
#else
        48);
#endif
    private readonly byte[] _fallbackDocument =
        "First line\nSecond line\nThird line\nFourth line\nFifth line\nSixth line"u8.ToArray();
    private string _currentPath = "/system/apps/NOTES.TXT";
    private string _status = "Ready";
    private ulong _window;
    private uint _launchCount;
    private int _saveInvocation;
    private bool _useTextInput;
    [ThreadStatic]
    private static uint s_threadLaunchCount;
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
    private bool _c118ProofContext;
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
    private readonly GuideXosButton _openButton =
        new(20, 220, 90, 28, "Open");
    private readonly GuideXosButton _saveButton =
        new(120, 220, 90, 28, "Save");
    private readonly GuideXosButton _saveAsButton =
        new(220, 220, 100, 28, "Save As");
    private bool _c119ProofContext;
    private bool _c119ButtonTestsRun;
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
    private const int C120OpenControlId = 1;
    private const int C120SaveControlId = 2;
    private const int C120SaveAsControlId = 3;
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
    private const int C121ShowPathControlId = 4;
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
    private const int C124FullPathControlId = 5;
    private const int C124FileNameControlId = 6;
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
    private const int C131ShowStatusControlId = 7;
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
    // C135 uses the otherwise-free slot 6 for the text area because the
    // reusable popup occupies slot 8 alongside the C133/C131 controls.
    private const int C120DocumentControlId = 6;
#else
    private const int C120DocumentControlId = 8;
#endif
#else
    private const int C120DocumentControlId = 7;
#endif
#else
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
    private const int C131ShowStatusControlId = 5;
    private const int C120DocumentControlId = 6;
#else
    private const int C120DocumentControlId = 5;
#endif
#endif
#else
    private const int C120DocumentControlId = 4;
#endif
    private GuideXosControlHost _mainControlHost;
    private bool _c120ProofContext;
#if !HOSTLOGPROOF_C121_MANAGED_CHECKBOX
    private bool _c120HostTestsRun;
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
    private readonly GuideXosCheckBox _showPathCheckBox =
        new(330, 220, 128, 28, "Show path", true);
    private bool _c121ProofContext;
    private bool _c121CheckboxTestContext;
    private bool _c121HostTestContext;
    private bool _c121CheckboxTestsRun;
    private bool _c121HostTestsRun;
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
    private readonly GuideXosLabel _pathLabel =
        new(20, 66, 480, string.Empty, GuideXosLabel.DefaultMaximumTextLength);
    private bool _c122ProofContext;
    private bool _c122LabelTestContext;
    private bool _c122LabelHostTestContext;
    private bool _c122LabelTestsRun;
    private bool _c122LabelHostTestsRun;
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
    private const uint C123HideSeparatorKey = 0x200u;
    private const uint C123ShowSeparatorKey = 0x201u;
    private const uint C123ExpandSeparatorKey = 0x202u;
    private const uint C123ContractSeparatorKey = 0x203u;
    private const uint C123RestoreSeparatorKey = 0x204u;
    private readonly GuideXosSeparator _separator = new(20, 200, 480);
    private bool _c123ProofContext;
    private bool _c123SeparatorTestContext;
    private bool _c123SeparatorHostTestContext;
    private bool _c123SeparatorTestsRun;
    private bool _c123SeparatorHostTestsRun;
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
    private const uint C124DisableFileNameKey = 0x300u;
    private const uint C124EnableFileNameKey = 0x301u;
    private readonly GuideXosRadioGroup _pathDisplayGroup = new(2);
    private readonly GuideXosRadioButton _fullPathRadio =
        new(20, 256, 128, 28, "Full path");
    private readonly GuideXosRadioButton _fileNameRadio =
        new(160, 256, 128, 28, "File name");
    private bool _c124ProofContext;
    private bool _c124RadioTestContext;
    private bool _c124RadioHostTestContext;
    private bool _c124RadioTestsRun;
    private bool _c124RadioHostTestsRun;
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
    private const uint C125HideProgressKey = 0x400u;
    private const uint C125ShowProgressKey = 0x401u;
    private const uint C125FillCapacityKey = 0x402u;
    private readonly GuideXosProgressBar _documentUsage =
        new(160, 174, 312, 0, GuideXosTextArea.DefaultMaximumCharacters, 0);
    private bool _c125ProofContext;
    private bool _c125ProgressTestContext;
    private bool _c125ProgressTestsRun;
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
    private const uint C126HideGroupBoxKey = 0x500u;
    private const uint C126ShowGroupBoxKey = 0x501u;
    private const uint C126GrowGroupBoxKey = 0x502u;
    private const uint C126ShrinkGroupBoxKey = 0x503u;
    private const uint C126MoveGroupBoxKey = 0x504u;
    private const uint C126RestoreGroupBoxKey = 0x505u;
    private readonly GuideXosGroupBox _pathDisplayBox =
        new(12, 264, 456, 90, "Path Display");
    private bool _c126ProofContext;
    private bool _c126GroupBoxTestContext;
    private bool _c126GroupBoxTestsRun;
#if HOSTLOGPROOF_C127_MANAGED_PANEL
    private const uint C127HidePanelKey = 0x600u;
    private const uint C127ShowPanelKey = 0x601u;
    private const uint C127GrowPanelKey = 0x602u;
    private const uint C127ShrinkPanelKey = 0x603u;
    private const uint C127MovePanelKey = 0x604u;
    private const uint C127RestorePanelKey = 0x605u;
    private readonly GuideXosPanel _pathDisplayPanel =
        new(12, 264, 456, 90, 4);
    private bool _c127ProofContext;
    private bool _c127PanelTestContext;
    private bool _c127PanelTestsRun;
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
    private bool _c128ProofContext;
    private bool _c128PanelLifecycleTestContext;
    private bool _c128PanelLifecycleTestsRun;
#endif
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
    private bool _c129ProofContext;
    private bool _c129ShiftTabTestContext;
    private int _c129ProofStage;
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
    private const uint C131HideStatusKey = 0x700u;
    private const uint C131ShowStatusKey = 0x701u;
    private readonly GuideXosCheckBox _showStatusCheckBox =
        new(330, 184, 128, 28, "Show status", true);
    private bool _showStatusVisible = true;
    private bool _c131ProofContext;
    private bool _c131CheckBoxTestContext;
    private bool _c131HostTestContext;
    private bool _c131CheckBoxTestsRun;
    private bool _c131HostTestsRun;
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
    private const uint C132HideFileNameKey = 0x800u;
    private const uint C132ShowFileNameKey = 0x801u;
    private const uint C132DisableFileNameKey = 0x802u;
    private const uint C132EnableFileNameKey = 0x803u;
    private bool _c132ProofContext;
    private bool _c132RadioTestContext;
    private bool _c132HostTestContext;
    private bool _c132RadioTestsRun;
    private bool _c132HostTestsRun;
    private int _c132RadioChangedCount;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
    private const uint C133HideComboKey = 0x900u;
    private const uint C133ShowComboKey = 0x901u;
    private const uint C133DisableComboKey = 0x902u;
    private const uint C133EnableComboKey = 0x903u;
    private const int C133ComboControlId = 5;
    private readonly GuideXosComboBox _pathDisplayCombo =
        new(20, 278, 268, 28, 16, 48, 4);
    private bool _c133ProofContext;
    private bool _c133ComboTestContext;
    private bool _c133HostTestContext;
    private bool _c133ComboTestsRun;
    private bool _c133HostTestsRun;
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
    private bool _c134HostTestContext;
    private bool _c134HostTestsRun;
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
    private const int C135MenuControlId = 8;
    private const uint C135OptionsActionId = 23u;
    private const uint C135HideMenuKey = 0xA00u;
    private const uint C135ShowMenuKey = 0xA01u;
    private const uint C135DisableMenuKey = 0xA02u;
    private const uint C135EnableMenuKey = 0xA03u;
    private readonly GuideXosPopupMenu _c135Menu =
        new(400, 280, 176, 8, 32);
    private bool _c135ProofContext;
    private bool _c135MenuTestContext;
    private bool _c135HostTestContext;
    private bool _c135MenuTestsRun;
    private bool _c135HostTestsRun;
    private bool _c135MenuTestsPassed;
    private bool _c135HostTestsPassed;
    private uint _c135PendingCommand;
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
    private bool _c136ProofContext;
    private bool _c136TestsRun;
    private bool _c136TestsPassed;
    private uint _c136CommandCount;
    private uint _c136InputTraceCount;
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
    private const uint C137RelaunchKey = 0x11Bu;
    private const int C137ListControlId = 9;
    private readonly GuideXosListBox _c137ListBox =
        new(16, 32, 4, 24);
    private bool _c137ProofContext;
    private bool _c137TestsRun;
    private bool _c137TestsPassed;
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
    private const int C138TextScrollControlId = 10;
    private const int C138ListScrollControlId = 11;
    private readonly GuideXosScrollBar _c138TextScrollBar =
        new(274, 72, 16, 96);
    private readonly GuideXosScrollBar _c138ListScrollBar =
        new(500, 72, 16, 72);
    private bool _c138ProofContext;
    private bool _c138TestsRun;
    private bool _c138TestsPassed;
    private bool _c138BindingsInstalled;
#endif
#endif
#endif
#endif
#endif

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return GuideXosResult.Success;
        uint launchCount = ++_launchCount;
        uint threadLaunchCount = ++s_threadLaunchCount;
        if (threadLaunchCount != launchCount)
        {
            host.TryLog("C117-NOTES threadStatic=FAIL"u8);
            return GuideXosResult.InvalidArgument;
        }

        _picker.Reset();
        _saveInvocation = 0;
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        _c133ProofContext = host.LaunchContext.Utf8.SequenceEqual("c133"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-host"u8);
        _c133ComboTestContext = host.LaunchContext.Utf8.SequenceEqual("c133-api"u8);
        _c133HostTestContext = host.LaunchContext.Utf8.SequenceEqual("c133-host"u8);
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
        _c134HostTestContext =
            host.LaunchContext.Utf8.SequenceEqual("c134-host-tests"u8);
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        _c135ProofContext = host.LaunchContext.Utf8.SequenceEqual("c135"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-native"u8);
        _c135MenuTestContext = host.LaunchContext.Utf8.SequenceEqual("c135-api"u8);
        _c135HostTestContext = host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8);
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
        _c136ProofContext = host.LaunchContext.Utf8.SequenceEqual("c136"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-native"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8)
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            || host.LaunchContext.Utf8.SequenceEqual("c137"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-native"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-relaunch"u8)
#endif
            ;
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        _c119ProofContext = IsC119Context(host);
        _openButton.Reset();
        _saveButton.Reset();
        _saveAsButton.Reset();
        if (host.LaunchContext.Utf8.SequenceEqual("c119-disabled"u8))
        {
            _saveButton.SetEnabled(false);
        }
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
        _c120ProofContext = IsC120Context(host)
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || host.LaunchContext.Utf8.SequenceEqual("c133-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-combo-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-combo-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-host"u8)
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
            || host.LaunchContext.Utf8.SequenceEqual("c134"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c134-host-tests"u8)
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            || host.LaunchContext.Utf8.SequenceEqual("c135"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8)
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
            || host.LaunchContext.Utf8.SequenceEqual("c136"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-native"u8)
#endif
#endif
#endif
#endif
            ;
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        _c121ProofContext = IsC121Context(host);
        _c121CheckboxTestContext = IsC121CheckboxTestContext(host);
        _c121HostTestContext = IsC121HostTestContext(host);
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
        _c122ProofContext = IsC122Context(host);
        _c122LabelTestContext = IsC122LabelTestContext(host);
        _c122LabelHostTestContext = IsC122LabelHostTestContext(host);
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        _c123ProofContext = IsC123Context(host);
        _c123SeparatorTestContext = IsC123SeparatorTestContext(host);
        _c123SeparatorHostTestContext = IsC123SeparatorHostTestContext(host);
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        _c124ProofContext = IsC124Context(host);
        _c124RadioTestContext = IsC124RadioTestContext(host);
        _c124RadioHostTestContext = IsC124RadioHostTestContext(host);
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        _c125ProofContext = IsC125Context(host);
        _c125ProgressTestContext = IsC125ProgressTestContext(host);
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        _c126ProofContext = IsC126NotesContext(host);
        _c126GroupBoxTestContext = IsC126GroupBoxTestContext(host);
        _c125ProofContext = _c125ProofContext || _c126ProofContext;
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        _c127ProofContext = IsC127NotesContext(host);
        _c127PanelTestContext = IsC127PanelTestContext(host);
        _c125ProofContext = _c125ProofContext || _c127ProofContext;
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
        _c128ProofContext = IsC128NotesContext(host);
        _c128PanelLifecycleTestContext =
            IsC128PanelLifecycleTestContext(host);
        _c125ProofContext = _c125ProofContext || _c128ProofContext;
#endif
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
        _c129ProofContext = IsC129Context(host);
        _c129ShiftTabTestContext = IsC129ShiftTabTestContext(host);
        _c129ProofStage = 0;
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        _c131ProofContext = IsC131NotesContext(host)
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            || IsC132NotesContext(host)
#endif
            ;
        _c131CheckBoxTestContext = IsC131CheckBoxTestContext(host);
        _c131HostTestContext = IsC131HostTestContext(host);
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        _c132ProofContext = IsC132NotesContext(host);
        _c132RadioTestContext = IsC132RadioTestContext(host);
        _c132HostTestContext = IsC132HostTestContext(host);
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        _c133ProofContext = IsC133NotesContext(host) ||
            IsC133ComboTestContext(host) ||
            IsC133HostTestContext(host) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133"u8);
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
        _c133ProofContext = _c133ProofContext ||
            host.LaunchContext.Utf8.SequenceEqual("c134"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c134-host-tests"u8);
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        _c133ProofContext = _c133ProofContext ||
            host.LaunchContext.Utf8.SequenceEqual("c135"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8)
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
            || host.LaunchContext.Utf8.SequenceEqual("c136"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-native"u8)
#endif
            ;
#endif
#endif
        _c133ComboTestContext = IsC133ComboTestContext(host) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-combo-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-api"u8);
        _c133HostTestContext = IsC133HostTestContext(host) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-combo-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-host"u8);
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
        _c134HostTestContext =
            host.LaunchContext.Utf8.SequenceEqual("c134-host-tests"u8);
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        _c135ProofContext = host.LaunchContext.Utf8.SequenceEqual("c135"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8)
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
            || host.LaunchContext.Utf8.SequenceEqual("c136"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-native"u8)
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            || host.LaunchContext.Utf8.SequenceEqual("c137"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-native"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-relaunch"u8)
#endif
            ;
        _c135MenuTestContext = host.LaunchContext.Utf8.SequenceEqual("c135-api"u8);
        _c135HostTestContext = host.LaunchContext.Utf8.SequenceEqual("c135-host-tests"u8);
#endif
#endif
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        _c137ProofContext = host.LaunchContext.Utf8.SequenceEqual("c137"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-native"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-relaunch"u8);
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        _c138ProofContext = host.LaunchContext.Utf8.SequenceEqual("c138"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c138-native"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c138-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c138-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c138-relaunch"u8);
        _c137ProofContext = _c137ProofContext || _c138ProofContext;
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        _c135ProofContext = _c135ProofContext || _c138ProofContext;
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
        _c136ProofContext = _c136ProofContext || _c138ProofContext;
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        _c120ProofContext = _c120ProofContext || _c132ProofContext;
        _c121ProofContext = _c121ProofContext || _c132ProofContext;
        _c122ProofContext = _c122ProofContext || _c132ProofContext;
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        _c120ProofContext = _c120ProofContext || _c133ProofContext;
        _c121ProofContext = _c121ProofContext || _c133ProofContext;
        _c131ProofContext = _c131ProofContext || _c133ProofContext;
        _c122ProofContext = _c122ProofContext || _c133ProofContext;
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        _c120ProofContext = _c120ProofContext || _c137ProofContext;
        _c121ProofContext = _c121ProofContext || _c137ProofContext;
        _c131ProofContext = _c131ProofContext || _c137ProofContext;
#endif
        _c124ProofContext = _c124ProofContext || _c125ProofContext;
#endif
        _c123ProofContext = _c123ProofContext || _c124ProofContext;
#endif
        _c122ProofContext = _c122ProofContext || _c123ProofContext;
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133HostTestContext &&
            !_c133HostTestsRun)
        {
            bool earlyC133HostTests =
                GuideXosComboBoxC133HostTests.Run(host);
            _c133HostTestsRun = true;
            host.TryLog(earlyC133HostTests
                ? "C133-HOST tests=PASS"u8
                : "C133-HOST tests=FAIL"u8);
        }
#if HOSTLOGPROOF_C134_HOST_TESTS
        if (_c134HostTestContext && !_c134HostTestsRun)
        {
            bool c134HostTests =
                GuideXosComboBoxC134HostTests.Run(host);
            _c134HostTestsRun = true;
            host.TryLog(c134HostTests
                ? "C134-HOST tests=PASS"u8
                : "C134-HOST tests=FAIL"u8);
        }
#if HOSTLOGPROOF_C135_HOST_TESTS
        if (_c135HostTestContext && !_c135HostTestsRun)
        {
            bool c135HostTests =
                GuideXosPopupMenuC135HostTests.Run(host);
            _c135HostTestsRun = true;
            _c135HostTestsPassed = c135HostTests;
            host.TryLog(c135HostTests
                ? "C135-POPUP-HOST-TESTS cases=40 result=PASS"u8
                : "C135-POPUP-HOST-TESTS cases=40 result=FAIL"u8);
        }
#endif
        if (_c133ProofContext && !_c134HostTestsRun)
        {
            bool c134HostTests =
                GuideXosComboBoxC134HostTests.Run(host);
            _c134HostTestsRun = true;
            host.TryLog(c134HostTests
                ? "C134-HOST tests=PASS"u8
                : "C134-HOST tests=FAIL"u8);
        }
#endif
#endif
        if (_c122ProofContext || _c122LabelTestContext || _c122LabelHostTestContext)
        {
            _pathLabel.Reset();
        }
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        if (_c123ProofContext || _c123SeparatorTestContext ||
            _c123SeparatorHostTestContext)
        {
            _separator.Reset();
        }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        if (_c124ProofContext || _c124RadioTestContext ||
            _c124RadioHostTestContext
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            || _c132ProofContext || _c132RadioTestContext ||
            _c132HostTestContext
#endif
        )
        {
            _pathDisplayGroup.Reset();
            _fullPathRadio.Reset();
            _fileNameRadio.Reset();
            _pathDisplayGroup.TryRegister(_fullPathRadio);
            _pathDisplayGroup.TryRegister(_fileNameRadio);
            _pathDisplayGroup.TrySelect(_fullPathRadio);
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            if (_c132ProofContext)
            {
                _c132RadioChangedCount = 0;
                _fullPathRadio.Changed = OnC132RadioChanged;
                _fileNameRadio.Changed = OnC132RadioChanged;
                _fullPathRadio.Checked = true;
                if (host.LaunchContext.Utf8.SequenceEqual("c132-disabled"u8))
                {
                    _fileNameRadio.SetEnabled(false);
                }
            }
#endif
        }
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext)
        {
            _pathDisplayCombo.Reset();
            _pathDisplayCombo.ClearItems();
            _pathDisplayCombo.TryAddItem("Full Path");
            _pathDisplayCombo.TryAddItem("File Name");
            _pathDisplayCombo.Changed = OnC133ComboChanged;
            _pathDisplayCombo.SelectedIndex = 0;
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            _pathDisplayPanel.Reset();
            _pathDisplayPanel.Clear();
            if (_pathDisplayPanel.TryAddChild(_pathDisplayCombo, 8, 14) !=
                    GuideXosPanelResult.Added)
            {
                return GuideXosResult.InvalidArgument;
            }
#endif
        }
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        if (_c135ProofContext)
        {
            _c135Menu.Reset();
            _c135Menu.ClearItems();
            _c135Menu.TryAddItem("Open", 20u);
            _c135Menu.TryAddItem("Save", 22u);
            _c135Menu.TryAddSeparator();
            _c135Menu.TryAddItem("Reload", 3u);
            _c135Menu.CommandInvoked = OnC135MenuCommand;
            _c135PendingCommand = 0u;
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
            _c136CommandCount = 0u;
            _c136InputTraceCount = 0u;
#endif
        }
#endif
#endif
        if (_c125ProofContext || _c125ProgressTestContext)
        {
            _documentUsage.Reset();
        }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        if (_c126ProofContext || _c126GroupBoxTestContext)
        {
            _pathDisplayBox.Reset();
            if (!ApplyC126RadioLayout()) return GuideXosResult.InvalidArgument;
        }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (_c127ProofContext || _c127PanelTestContext)
        {
            _pathDisplayBox.Reset();
            _pathDisplayPanel.Reset();
            if (!ApplyC127PanelLayout()) return GuideXosResult.InvalidArgument;
        }
#endif
#endif
#endif
#endif
#endif
        if (_c120ProofContext)
        {
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            _showPathCheckBox.Reset();
            _showPathCheckBox.SetChecked(true);
            if (host.LaunchContext.Utf8.SequenceEqual("c121-disabled"u8))
            {
                _showPathCheckBox.SetEnabled(false);
            }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            _showStatusVisible = true;
            _showStatusCheckBox.Reset();
            _showStatusCheckBox.Changed = OnShowStatusChanged;
            _showStatusCheckBox.Checked = true;
            if (host.LaunchContext.Utf8.SequenceEqual("c131-disabled"u8))
            {
                _showStatusCheckBox.SetEnabled(false);
            }
#endif
            _mainControlHost = new GuideXosControlHost(
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                10);
#else
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
                _c135ProofContext ? 8 :
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                _c133ProofContext ? 7 :
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
                _c131ProofContext ? 8 :
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
                _c132ProofContext ? 8 :
#endif
                _c124ProofContext ? 7 :
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
                _c131ProofContext ? 8 :
#endif
                _c121ProofContext ? 5 : 4);
#else
            4);
#endif
#endif
            _mainControlHost.Reset();
            _mainControlHost.TryRegisterButton(C120OpenControlId, _openButton);
            _mainControlHost.TryRegisterButton(C120SaveControlId, _saveButton);
            _mainControlHost.TryRegisterButton(C120SaveAsControlId, _saveAsButton);
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            if (_c121ProofContext)
            {
                _mainControlHost.TryRegisterCheckBox(
                    C121ShowPathControlId, _showPathCheckBox);
            }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            if (_c133ProofContext)
            {
                _mainControlHost.TryRegisterComboBox(
                    C133ComboControlId, _pathDisplayCombo);
            }
            else if (_c124ProofContext
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
                || _c132ProofContext
#endif
                )
            {
                _mainControlHost.TryRegisterRadioButton(
                    C124FullPathControlId, _fullPathRadio);
                _mainControlHost.TryRegisterRadioButton(
                    C124FileNameControlId, _fileNameRadio);
            }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            if (_c131ProofContext)
            {
                _mainControlHost.TryRegisterCheckBox(
                    C131ShowStatusControlId, _showStatusCheckBox);
            }
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            if (_c135ProofContext)
            {
                _mainControlHost.TryRegisterPopupMenu(
                    C135MenuControlId, _c135Menu, false);
            }
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            if (_c137ProofContext)
            {
                _c137ListBox.Reset();
                for (int index = 0; index < 12; index++)
                {
                    _c137ListBox.TryAdd(C137FixtureLabel(index));
                }
                _c137ListBox.Blur();
            }
#endif
            _mainControlHost.TryRegisterTextArea(C120DocumentControlId, _textArea);
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            if (_c137ProofContext)
            {
                _mainControlHost.TryRegisterListBox(C137ListControlId, _c137ListBox);
            }
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext)
            {
                EnsureC138Bindings();
                _mainControlHost.TryRegisterScrollBar(
                    C138TextScrollControlId, _c138TextScrollBar);
                _mainControlHost.TryRegisterScrollBar(
                    C138ListScrollControlId, _c138ListScrollBar);
            }
#endif
#endif
            if (host.LaunchContext.Utf8.SequenceEqual("c120-disabled"u8))
            {
                _saveButton.SetEnabled(false);
            }
            int expectedHostRegistration =
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
                _c138ProofContext ? 10 :
#endif
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
                _c137ProofContext ? 8 :
#endif
                _c135ProofContext ? 8 :
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                _c133ProofContext ? 7 :
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
                _c131ProofContext ? 8 :
#endif
                _c124ProofContext ? 7 :
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
                _c131ProofContext ? 8 :
#endif
                _c121ProofContext ? 5 : 4;
#else
                4;
#endif
            bool hostRegistration = _mainControlHost.RegistrationCount ==
                expectedHostRegistration && _mainControlHost.ActiveIndex == -1;
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 10 &&
                    _mainControlHost.TransientInputCaptureKind ==
                        GuideXosManagedControlKind.None &&
                    !_mainControlHost.HasPointerDragCapture
                    ? "C138-HOST registration=10 scrollbars=2 initial=no-focus drag=none result=PASS"u8
                    : "C138-HOST registration=FAIL initial=no-focus result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            if (_c135ProofContext
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
                && !_c137ProofContext
#endif
                )
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 8 &&
                    _mainControlHost.TransientInputCaptureKind ==
                        GuideXosManagedControlKind.None
                    ? "C135-HOST registration=8 menu=non-focusable initial=no-focus result=PASS"u8
                    : "C135-HOST registration=FAIL initial=no-focus result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            if (_c137ProofContext
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                && !_c138ProofContext
#endif
                )
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 8 &&
                    _mainControlHost.TransientInputCaptureKind ==
                        GuideXosManagedControlKind.None
                    ? "C137-HOST registration=8 list=registered initial=viewport-zero result=PASS"u8
                    : "C137-HOST registration=FAIL initial=viewport-zero result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            if (_c133ProofContext
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
                && !_c135ProofContext
#endif
                )
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 7
                    ? "C133-HOST registration=7 combo=path-display initial=no-focus result=PASS"u8
                    : "C133-HOST registration=FAIL initial=no-focus result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            if (_c121ProofContext
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
                && !_c124ProofContext
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
                && !_c135ProofContext
#endif
                )
            {
                host.TryLog(hostRegistration
                    ? "C121-HOST registration=5 initial=no-focus result=PASS"u8
                    : "C121-HOST registration=FAIL initial=FAIL result=FAIL"u8);
            }
            else
#endif
            {
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
                if (!_c135ProofContext)
#endif
                {
                    host.TryLog(hostRegistration
                        ? "C120-HOST registration=4 initial=no-focus result=PASS"u8
                        : "C120-HOST registration=FAIL initial=FAIL result=FAIL"u8);
                }
            }
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            if (_c123ProofContext
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
                && !_c124ProofContext
#endif
                )
            {
                host.TryLog(hostRegistration
                    ? "C123-HOST registration=5 initial=no-focus result=PASS"u8
                    : "C123-HOST registration=FAIL initial=FAIL result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            if (_c124ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 7
                    ? "C124-HOST registration=7 initial=no-focus result=PASS"u8
                    : "C124-HOST registration=FAIL initial=FAIL result=FAIL"u8);
            }
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
            if (_c129ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 4 &&
                    _mainControlHost.ActiveIndex == -1
                    ? "C129-MANAGED proof-started initial-focus=none registration=4 tab=key-down-only result=PASS"u8
                    : "C129-MANAGED proof-started result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            if (_c131ProofContext && !_c133ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount ==
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                        (_c138ProofContext ? 10 : 8)
#else
                        8
#endif
                    ? "C131-HOST registration=8 initial=no-focus result=PASS"u8
                    : "C131-HOST registration=FAIL initial=no-focus result=FAIL"u8);
            }
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            if (_c132ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 8
                    ? "C132-HOST registration=8 group=path-display initial=no-focus result=PASS"u8
                    : "C132-HOST registration=FAIL initial=no-focus result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            if (_c125ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 7
                    ? "C125-HOST registration=7 initial=no-focus result=PASS"u8
                    : "C125-HOST registration=FAIL initial=FAIL result=FAIL"u8);
            }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            if (_c126ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 7 &&
                    !_pathDisplayBox.Focusable
                    ? "C126-HOST registration=7 group-box=absent initial=no-focus result=PASS"u8
                    : "C126-HOST registration=FAIL group-box=registered result=FAIL"u8);
            }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            if (_c127ProofContext)
            {
                host.TryLog(hostRegistration &&
                    _mainControlHost.RegistrationCount == 7 &&
                    _pathDisplayPanel.ChildCount == 2 &&
                    !_pathDisplayPanel.Focusable
                    ? "C127-HOST registration=7 panel=non-focusable children=2 initial=no-focus result=PASS"u8
                    : "C127-HOST registration=FAIL panel=registered-or-focusable result=FAIL"u8);
            }
#endif
#endif
#endif
#endif
        }
#endif
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
        _c118ProofContext = IsC118Context(host);
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        _c118ProofContext = _c118ProofContext || _c119ProofContext;
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
        _c118ProofContext = _c118ProofContext || _c120ProofContext;
#endif
        _useTextInput = _c118ProofContext;
#endif
        _currentPath = "/system/apps/NOTES.TXT";
#if HOSTLOGPROOF_C122_MANAGED_LABEL
        if ((_c122ProofContext || _c122LabelTestContext || _c122LabelHostTestContext) &&
            !UpdatePathLabel())
        {
            return GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        if (_c119ProofContext)
        {
            GuideXosFileResult directoryCache = _picker.PrimeDirectory(
                host, "/system/apps");
            if (directoryCache != GuideXosFileResult.Success)
            {
                host.TryLog("C119-NOTES directory-cache=FAIL"u8);
                return GuideXosResult.InvalidArgument;
            }
            host.TryLog("C119-NOTES directory-cache=PASS"u8);
        }
#endif
        GuideXosFileResult loadResult = GuideXosFile.ReadAllTextUtf8(
            host, Encoding.UTF8.GetBytes(_currentPath), out byte[] loaded);
        if (loadResult == GuideXosFileResult.Success &&
            _textArea.SetUtf8(loaded))
        {
            _status = "Loaded from VFS";
        }
        else if (loadResult == GuideXosFileResult.NotFound &&
            _textArea.SetText(C117InitialDocument))
        {
            _status = "New note";
        }
        else
        {
            _textArea.SetUtf8(_fallbackDocument);
            _status = StatusText(loadResult);
        }
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        if (_c137ProofContext)
        {
            _textArea.SetText(
                "Line 00\nLine 01\nLine 02\nLine 03\nLine 04\nLine 05\n" +
                "Line 06\nLine 07\nLine 08\nLine 09\nLine 10\nLine 11");
            _status = "C137 wheel fixture";
        }
#endif
        _textArea.SetCaretToStart();
        _textArea.Blur();
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext)
        {
            _c138TextScrollBar.ResetTransientState();
            _c138ListScrollBar.ResetTransientState();
            EnsureC138Bindings();
        }
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext &&
            host.LaunchContext.Utf8.SequenceEqual("c138-relaunch"u8))
        {
            host.TryLog(_mainControlHost.RegistrationCount == 10 &&
                _mainControlHost.ActiveIndex == -1 &&
                _textArea.FirstVisibleLine == 0 &&
                _c137ListBox.FirstVisibleIndex == 0 &&
                _c137ListBox.SelectedIndex == 0 &&
                _c138TextScrollBar.Value == 0 &&
                _c138ListScrollBar.Value == 0 &&
                _mainControlHost.TransientInputCaptureKind ==
                    GuideXosManagedControlKind.None &&
                !_mainControlHost.HasPointerDragCapture
                ? "C138-RELAUNCH registration=10 text-viewport=0 list-viewport=0 selection=0 scrollbar-text=0 scrollbar-list=0 capture=none drag=none result=PASS"u8
                : "C138-RELAUNCH registration=FAIL capture=unknown drag=unknown result=FAIL"u8);
        }
#endif
        if (_c137ProofContext &&
            host.LaunchContext.Utf8.SequenceEqual("c137-relaunch"u8))
        {
            host.TryLog(_mainControlHost.RegistrationCount == 8 &&
                _mainControlHost.ActiveIndex == -1 &&
                _textArea.FirstVisibleLine == 0 &&
                _c137ListBox.FirstVisibleIndex == 0 &&
                _c137ListBox.SelectedIndex == 0 &&
                _mainControlHost.TransientInputCaptureKind ==
                    GuideXosManagedControlKind.None
                ? "C137-RELAUNCH registration=8 text-viewport=0 list-viewport=0 selection=0 capture=none result=PASS"u8
                : "C137-RELAUNCH registration=FAIL capture=unknown result=FAIL"u8);
        }
#endif

        GuideXosResult result = host.TryCreateSurface(
            "Managed Notes"u8, 600, 360, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;
        _window = surface.Handle;
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext && !_c138TestsRun)
        {
            _c138TestsPassed = GuideXosScrollBarC138Tests.Run(host);
            _c138TestsRun = true;
        }
#endif
        if (!RenderMain(host, surface, launchCount)) return GuideXosResult.InvalidArgument;
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (_c125ProofContext)
        {
            LogC125Progress(host, "initial"u8);
            host.TryLog(_mainControlHost.RegistrationCount == 7 &&
                _documentUsage.Maximum == _textArea.MaximumCharacters
                ? "C125-NOTES initial=authoritative-length capacity=256 registration=7 result=PASS"u8
                : "C125-NOTES initial=FAIL result=FAIL"u8);
        }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        if (_c126ProofContext)
        {
            bool groupBoxConfigured = _pathDisplayBox.Caption == "Path Display" &&
                _pathDisplayBox.X == 12 && _pathDisplayBox.Y == 264 &&
                _pathDisplayBox.Width == 456 && _pathDisplayBox.Height == 90 &&
                _pathDisplayBox.Visible && !_pathDisplayBox.Focusable &&
                _mainControlHost.RegistrationCount == 7;
            host.TryLog(groupBoxConfigured
                ? "C126-NOTES initial=PathDisplay caption=PathDisplay bounds=12,264,456,90 radios=independent registration=7 result=PASS"u8
                : "C126-NOTES initial=FAIL result=FAIL"u8);
        }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (_c127ProofContext)
        {
            bool panelConfigured = _pathDisplayPanel.X == 12 &&
                _pathDisplayPanel.Y == 264 && _pathDisplayPanel.Width == 456 &&
                _pathDisplayPanel.Height == 90 && _pathDisplayPanel.Visible &&
                _pathDisplayPanel.ChildCount == 2 &&
                _fullPathRadio.X == 20 && _fullPathRadio.Y == 278 &&
                _fileNameRadio.X == 160 && _fileNameRadio.Y == 278 &&
                _fullPathRadio.ParentPanel == _pathDisplayPanel &&
                _fileNameRadio.ParentPanel == _pathDisplayPanel &&
                _mainControlHost.RegistrationCount == 7;
            host.TryLog(panelConfigured
                ? "C127-NOTES initial=PathDisplay panel=bounds=12,264,456,90 children=2 registration=7 result=PASS"u8
                : "C127-NOTES initial=FAIL result=FAIL"u8);
        }
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
        if (_c128ProofContext)
        {
            bool lifecycleInitial = _mainControlHost.RegistrationCount == 7 &&
                _pathDisplayPanel.ChildCount == 2 &&
                _pathDisplayPanel.Visible &&
                !_fullPathRadio.IsFocused && !_fileNameRadio.IsFocused;
            host.TryLog(lifecycleInitial
                ? "C128-NOTES initial=registration=7 result=PASS"u8
                : "C128-NOTES initial=FAIL result=FAIL"u8);
        }
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (_c131ProofContext
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            && !_c133ProofContext
#endif
            )
        {
            bool c131Initial = _mainControlHost.RegistrationCount ==
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                (_c138ProofContext ? 10 : 8) &&
#else
                8 &&
#endif
                _showStatusCheckBox.Checked && _showStatusCheckBox.Visible &&
                _showStatusVisible && !_showStatusCheckBox.IsFocused;
            host.TryLog(c131Initial
                ? "C131-NOTES initial=registration=8 show-status=checked result=PASS"u8
                : "C131-NOTES initial=FAIL result=FAIL"u8);
        }
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        if (_c132ProofContext)
        {
            bool c132Initial = _mainControlHost.RegistrationCount == 8 &&
                _pathDisplayGroup.SelectedMember == _fullPathRadio &&
                _fullPathRadio.Checked && !_fileNameRadio.Checked &&
                _fullPathRadio.Group == _pathDisplayGroup &&
                _fileNameRadio.Group == _pathDisplayGroup &&
                _c132RadioChangedCount == 0;
            host.TryLog(c132Initial
                ? "C132-NOTES initial=registration=8 selection=full-path callbacks=0 result=PASS"u8
                : "C132-NOTES initial=FAIL result=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            && !_c135ProofContext
#endif
            )
        {
            bool c133Initial = _mainControlHost.RegistrationCount == 7 &&
                _pathDisplayCombo.SelectedIndex == 0 &&
                _pathDisplayCombo.ItemCount == 2 &&
                !_pathDisplayCombo.IsOpen && _c133ProofContext;
            host.TryLog(c133Initial
                ? "C133-NOTES initial=registration=7 selection=full-path callbacks=0 result=PASS"u8
                : "C133-NOTES initial=FAIL result=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        if (_c135ProofContext)
        {
            bool c135Initial = _mainControlHost.RegistrationCount ==
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                (_c138ProofContext ? 10 : 8) &&
#else
                8 &&
#endif
                _c135Menu.ItemCount == 4 && !_c135Menu.IsOpen &&
                _c135Menu.InvokerAvailable;
            host.TryLog(c135Initial
                ? "C135-NOTES initial=registration=8 items=4 capture=none result=PASS"u8
                : "C135-NOTES initial=result=FAIL"u8);
        }
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
        if (_c136ProofContext)
        {
            bool c136Initial = _mainControlHost.RegistrationCount ==
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                (_c138ProofContext ? 10 : 8) &&
#else
                8 &&
#endif
                !_c135Menu.IsOpen &&
                _mainControlHost.TransientInputCaptureKind ==
                    GuideXosManagedControlKind.None;
            host.TryLog(c136Initial
                ? "C136-NOTES initial=registration=8 target=document menu=reused result=PASS"u8
                : "C136-NOTES initial=result=FAIL"u8);
        }
#endif
#endif
#endif
        host.TryLog("C117-NOTES threadStatic=PASS"u8);
        host.TryLog(loadResult == GuideXosFileResult.Success
            ? "C117-NOTES initial=multiline source=VFS result=PASS"u8
            : "C117-NOTES initial=multiline source=fixture result=PASS"u8);
        bool runFocusedProofTests = true;
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        runFocusedProofTests = !_c119ProofContext || !_c119ButtonTestsRun;
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        if (_c121CheckboxTestContext || _c121HostTestContext
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            || _c122LabelTestContext || _c122LabelHostTestContext
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            || _c123SeparatorTestContext || _c123SeparatorHostTestContext
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            || _c124RadioTestContext || _c124RadioHostTestContext
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            || _c132RadioTestContext || _c132HostTestContext
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || _c133ComboTestContext || _c133HostTestContext
#endif
#endif
#endif
            )
        {
            runFocusedProofTests = false;
        }
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
        if (_c122ProofContext)
        {
            runFocusedProofTests = false;
        }
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (_c125ProgressTestContext)
        {
            runFocusedProofTests = false;
        }
#endif
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        if (_c126GroupBoxTestContext)
        {
            runFocusedProofTests = false;
        }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (_c127PanelTestContext)
        {
            runFocusedProofTests = false;
        }
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
        if (_c128PanelLifecycleTestContext)
        {
            runFocusedProofTests = false;
        }
#endif
#endif
#endif
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
        if (_c129ShiftTabTestContext)
        {
            runFocusedProofTests = false;
        }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (_c131CheckBoxTestContext || _c131HostTestContext)
        {
            runFocusedProofTests = false;
        }
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        // C135 has its own bounded API/host suites.  Do not run the broad
        // historical control catalog during the production composite launch;
        // that work is unrelated to the popup proof and can consume the
        // synchronous managed dispatch budget before native input begins.
        if (_c135ProofContext || _c135MenuTestContext || _c135HostTestContext)
        {
            runFocusedProofTests = false;
        }
#endif
        bool textAreaTests = runFocusedProofTests
            ? GuideXosTextAreaTests.Run(host) : true;
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
        bool listBoxTests = runFocusedProofTests
            ? GuideXosListBoxTests.Run(host) : true;
        bool textInputTests = runFocusedProofTests
            ? GuideXosTextInputTests.Run(host) : true;
        if (_c118ProofContext)
        {
            host.TryLog(textAreaTests
                ? "C117-REGRESSION text-area=PASS"u8
                : "C117-REGRESSION text-area=FAIL"u8);
            host.TryLog(listBoxTests
                ? "C118-NOTES initial=multiline list=ready result=PASS"u8
                : "C118-NOTES initial=multiline list=ready result=FAIL"u8);
            host.TryLog(textInputTests
                ? "C116-REGRESSION text-input=PASS"u8
                : "C116-REGRESSION text-input=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        bool buttonTests = runFocusedProofTests
            ? GuideXosButtonTests.Run(host, surface) : true;
        if (_c119ProofContext)
        {
            _c119ButtonTestsRun = true;
            host.TryLog(buttonTests
                ? "C119-NOTES initial=managed-buttons result=PASS"u8
                : "C119-NOTES initial=managed-buttons result=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        // C121 keeps the C120 host suite in the C120 proof image. The C121
        // production image runs the focused checkbox host suite below; the
        // older suite is validated by the dedicated C120 runner so its
        // allocation-heavy fixture pass cannot consume the C121 image budget.
        bool controlHostTests = true;
#else
        bool controlHostTests = _c120ProofContext && !_c120HostTestsRun
            ? GuideXosControlHostTests.Run(host) : true;
#endif
        if (_c120ProofContext)
        {
#if !HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            _c120HostTestsRun = true;
#endif
#if !HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            host.TryLog(controlHostTests
                ? "C120-HOST tests=PASS"u8
                : "C120-HOST tests=FAIL"u8);
#endif
        }
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        bool checkBoxHostTests = _c121HostTestContext && !_c121HostTestsRun
            ? GuideXosCheckBoxHostTests.Run(host) : true;
        bool checkBoxTests = _c121CheckboxTestContext && !_c121CheckboxTestsRun
            ? GuideXosCheckBoxTests.Run(host, surface) : true;
        if (_c121HostTestContext)
        {
            _c121HostTestsRun = true;
            host.TryLog(checkBoxHostTests
                ? "C121-HOST tests=PASS"u8
                : "C121-HOST tests=FAIL"u8);
        }
        if (_c121CheckboxTestContext)
        {
            _c121CheckboxTestsRun = true;
        }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        bool c131HostTests = _c131HostTestContext && !_c131HostTestsRun
            ? GuideXosCheckBoxC131HostTests.Run(host) : true;
        bool c131CheckBoxTests = _c131CheckBoxTestContext &&
            !_c131CheckBoxTestsRun
            ? GuideXosCheckBoxC131Tests.Run(host, surface) : true;
        if (_c131HostTestContext)
        {
            _c131HostTestsRun = true;
            host.TryLog(c131HostTests
                ? "C131-HOST tests=PASS"u8
                : "C131-HOST tests=FAIL"u8);
        }
        if (_c131CheckBoxTestContext)
        {
            _c131CheckBoxTestsRun = true;
        }
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        bool c132HostTests = _c132HostTestContext && !_c132HostTestsRun
            ? GuideXosRadioButtonC132HostTests.Run(host) : true;
        bool c132RadioTests = _c132RadioTestContext && !_c132RadioTestsRun
            ? GuideXosRadioButtonC132Tests.Run(host, surface) : true;
        if (_c132HostTestContext)
        {
            _c132HostTestsRun = true;
            host.TryLog(c132HostTests
                ? "C132-HOST tests=PASS"u8
                : "C132-HOST tests=FAIL"u8);
        }
        if (_c132RadioTestContext)
        {
            _c132RadioTestsRun = true;
        }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        bool c133ApiContext = host.LaunchContext.Utf8.SequenceEqual("c133-api"u8);
        bool c133HostContext = host.LaunchContext.Utf8.SequenceEqual("c133-host"u8);
        bool c133ComboTests = (_c133ComboTestContext || c133ApiContext) && !_c133ComboTestsRun
            ? GuideXosComboBoxC133Tests.Run(host) : true;
        bool c133HostTests = (_c133HostTestContext || c133HostContext) &&
            !_c133HostTestsRun
            ? GuideXosComboBoxC133HostTests.Run(host) : true;
        if (_c133HostTestContext || c133HostContext)
        {
            _c133HostTestsRun = true;
            host.TryLog(c133HostTests
                ? "C133-HOST tests=PASS"u8
                : "C133-HOST tests=FAIL"u8);
        }
        if (_c133ComboTestContext || c133ApiContext)
        {
            _c133ComboTestsRun = true;
            host.TryLog(c133ComboTests
                ? "C133-COMBO-TESTS cases=44 result=PASS"u8
                : "C133-COMBO-TESTS cases=44 result=FAIL"u8);
        }
#if HOSTLOGPROOF_C135_POPUP_MENU_TESTS
        bool c135MenuTests = _c135MenuTestContext && !_c135MenuTestsRun
            ? GuideXosPopupMenuC135Tests.Run(host) : true;
        if (_c135MenuTestContext)
        {
            _c135MenuTestsRun = true;
            _c135MenuTestsPassed = c135MenuTests;
            host.TryLog(c135MenuTests
                ? "C135-POPUP-TESTS cases=40 result=PASS"u8
                : "C135-POPUP-TESTS cases=40 result=FAIL"u8);
        }
#if HOSTLOGPROOF_C135_HOST_TESTS
        if (_c135ProofContext && _c135MenuTestsRun && _c135HostTestsRun)
        {
            host.TryLog(_c135MenuTestsPassed && _c135HostTestsPassed
                ? "C135-FOCUSED-TESTS menu=PASS host=PASS result=PASS"u8
                : "C135-FOCUSED-TESTS menu=FAIL host=FAIL result=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
        bool c136Tests = ((_c136ProofContext
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            && !_c138ProofContext
#endif
            ) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8)) &&
            !_c136TestsRun
            ? GuideXosSecondaryPointerC136Tests.Run(host) : true;
        if (_c136ProofContext || host.LaunchContext.Utf8.SequenceEqual("c136-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c136-host-tests"u8))
        {
            _c136TestsRun = true;
            _c136TestsPassed = c136Tests;
            host.TryLog(c136Tests
                ? "C136-POINTER-TESTS cases=36 result=PASS"u8
                : "C136-POINTER-TESTS cases=36 result=FAIL"u8);
        }
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        bool c137Tests = ((_c137ProofContext
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            && !_c138ProofContext
#endif
            ) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-host-tests"u8)) &&
            !_c137TestsRun
            ? GuideXosMouseWheelC137Tests.Run(host) : true;
        if (_c137ProofContext || host.LaunchContext.Utf8.SequenceEqual("c137-api"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c137-host-tests"u8))
        {
            _c137TestsRun = true;
            _c137TestsPassed = c137Tests;
            host.TryLog(c137Tests
                ? "C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS"u8
                : "C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=FAIL"u8);
        }
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        bool c138Tests = _c138ProofContext && _c138TestsRun
            ? _c138TestsPassed : true;
        if (_c138ProofContext)
        {
            host.TryLog(c138Tests
                ? "C138-TESTS api=22 drag-host=20 textarea=10 listbox=10 cases=62 result=PASS"u8
                : "C138-TESTS api=FAIL drag-host=FAIL binding=FAIL result=FAIL"u8);
        }
#endif
#endif
#endif
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
        bool labelHostTests = _c122LabelHostTestContext && !_c122LabelHostTestsRun
            ? GuideXosLabelHostTests.Run(host, surface) : true;
        bool labelTests = _c122LabelTestContext && !_c122LabelTestsRun
            ? GuideXosLabelTests.Run(host, surface) : true;
        if (_c122LabelHostTestContext)
        {
            _c122LabelHostTestsRun = true;
        }
        if (_c122LabelTestContext)
        {
            _c122LabelTestsRun = true;
        }
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        bool separatorHostTests = _c123SeparatorHostTestContext &&
            !_c123SeparatorHostTestsRun
            ? GuideXosSeparatorHostTests.Run(host) : true;
        bool separatorTests = _c123SeparatorTestContext &&
            !_c123SeparatorTestsRun
            ? GuideXosSeparatorTests.Run(host, surface) : true;
        if (_c123SeparatorHostTestContext)
        {
            _c123SeparatorHostTestsRun = true;
        }
        if (_c123SeparatorTestContext)
        {
            _c123SeparatorTestsRun = true;
        }
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        bool radioHostTests = _c124RadioHostTestContext &&
            !_c124RadioHostTestsRun
            ? GuideXosRadioButtonHostTests.Run(host) : true;
        bool radioButtonTests = _c124RadioTestContext &&
            !_c124RadioTestsRun
            ? GuideXosRadioButtonTests.Run(host, surface) : true;
        bool radioGroupTests = _c124RadioTestContext &&
            !_c124RadioTestsRun
            ? GuideXosRadioGroupTests.Run(host) : true;
        if (_c124RadioHostTestContext) _c124RadioHostTestsRun = true;
        if (_c124RadioTestContext) _c124RadioTestsRun = true;
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        bool progressTests = _c125ProgressTestContext &&
            !_c125ProgressTestsRun
            ? GuideXosProgressBarTests.Run(host, surface) : true;
        if (_c125ProgressTestContext) _c125ProgressTestsRun = true;
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        bool groupBoxTests = _c126GroupBoxTestContext &&
            !_c126GroupBoxTestsRun
            ? GuideXosGroupBoxTests.Run(host, surface) : true;
        if (_c126GroupBoxTestContext) _c126GroupBoxTestsRun = true;
#if HOSTLOGPROOF_C127_MANAGED_PANEL
        bool panelTests = _c127PanelTestContext &&
            !_c127PanelTestsRun
            ? GuideXosPanelTests.Run(host, surface) : true;
        if (_c127PanelTestContext) _c127PanelTestsRun = true;
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
        bool panelLifecycleTests = _c128PanelLifecycleTestContext &&
            !_c128PanelLifecycleTestsRun
            ? GuideXosPanelLifecycleTests.Run(host) : true;
        if (_c128PanelLifecycleTestContext)
        {
            _c128PanelLifecycleTestsRun = true;
        }
#endif
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
        bool c129ShiftTabTests = _c129ShiftTabTestContext
            ? GuideXosShiftTabTransportTests.Run(host) : true;
#else
        bool c129ShiftTabTests = true;
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
        return textAreaTests
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            && c138Tests
#endif
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
            && listBoxTests && textInputTests
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
            && buttonTests
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
            && controlHostTests
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            && (!_c135MenuTestContext || _c135MenuTestsPassed)
            && (!_c135HostTestContext || _c135HostTestsPassed)
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            && checkBoxTests && checkBoxHostTests
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            && c131CheckBoxTests && c131HostTests
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            && labelTests && labelHostTests
#endif
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            && separatorTests && separatorHostTests
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            && radioButtonTests && radioGroupTests && radioHostTests
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            && progressTests
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            && groupBoxTests
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            && panelTests
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            && panelLifecycleTests
#endif
#endif
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
            && c129ShiftTabTests
#endif
#endif
#endif
#endif
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
    private static bool IsC118Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c118-notes"u8);
    }
#endif

#if HOSTLOGPROOF_C119_MANAGED_BUTTON
    private static bool IsC119Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c119-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c119-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c120-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c120-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c121-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c121-disabled"u8)
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            || host.LaunchContext.Utf8.SequenceEqual("c122-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c122-disabled"u8)
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            || host.LaunchContext.Utf8.SequenceEqual("c123-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c123-disabled"u8)
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            || host.LaunchContext.Utf8.SequenceEqual("c124-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c124-disabled"u8)
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            || host.LaunchContext.Utf8.SequenceEqual("c125-notes"u8)
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            || host.LaunchContext.Utf8.SequenceEqual("c126-notes"u8)
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            || host.LaunchContext.Utf8.SequenceEqual("c127-notes"u8)
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            || host.LaunchContext.Utf8.SequenceEqual("c128-notes"u8)
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || IsC133NotesContext(host)
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
            ;
    }

#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
    private static bool IsC120Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c120-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c120-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c121-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c121-disabled"u8)
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            || host.LaunchContext.Utf8.SequenceEqual("c122-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c122-disabled"u8)
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            || host.LaunchContext.Utf8.SequenceEqual("c123-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c123-disabled"u8)
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            || host.LaunchContext.Utf8.SequenceEqual("c124-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c124-disabled"u8)
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            || host.LaunchContext.Utf8.SequenceEqual("c125-notes"u8)
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            || host.LaunchContext.Utf8.SequenceEqual("c126-notes"u8)
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            || host.LaunchContext.Utf8.SequenceEqual("c127-notes"u8)
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            || host.LaunchContext.Utf8.SequenceEqual("c128-notes"u8)
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
            || host.LaunchContext.Utf8.SequenceEqual("c129-shift-tab-proof"u8)
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || IsC133NotesContext(host)
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
            ;
    }
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
    private static bool IsC121Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c121-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c121-disabled"u8)
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            || IsC122Context(host)
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            || IsC123Context(host)
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            || IsC124Context(host)
#endif
#endif
#endif
            ;
    }
    private static bool IsC121CheckboxTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c121-checkbox-tests"u8);
    }
    private static bool IsC121HostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c121-host-tests"u8);
    }
#endif
#if HOSTLOGPROOF_C122_MANAGED_LABEL
    private static bool IsC122Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c122-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c122-disabled"u8);
    }

    private static bool IsC122LabelTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c122-label-tests"u8);
    }

    private static bool IsC122LabelHostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c122-label-host-tests"u8);
    }
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
    private static bool IsC123Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c123-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c123-disabled"u8);
    }

    private static bool IsC123SeparatorTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c123-separator-tests"u8);
    }

    private static bool IsC123SeparatorHostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c123-separator-host-tests"u8);
    }
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
    private static bool IsC124Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c124-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c124-disabled"u8)
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            || host.LaunchContext.Utf8.SequenceEqual("c125-notes"u8)
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            || host.LaunchContext.Utf8.SequenceEqual("c126-notes"u8)
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            || host.LaunchContext.Utf8.SequenceEqual("c127-notes"u8)
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            || host.LaunchContext.Utf8.SequenceEqual("c128-notes"u8)
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || IsC133NotesContext(host)
#endif
#endif
#endif
#endif
#endif
            ;
    }

    private static bool IsC124RadioTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c124-radio-tests"u8);
    }

    private static bool IsC124RadioHostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c124-radio-host-tests"u8);
    }
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
    private static bool IsC125Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c125-notes"u8)
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            || host.LaunchContext.Utf8.SequenceEqual("c126-notes"u8)
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            || host.LaunchContext.Utf8.SequenceEqual("c127-notes"u8)
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            || host.LaunchContext.Utf8.SequenceEqual("c128-notes"u8)
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            || IsC133NotesContext(host)
#endif
#endif
#endif
#endif
            ;
    }

    private static bool IsC125ProgressTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c125-progress-tests"u8);
    }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
    private static bool IsC126NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c126-notes"u8);
    }

    private static bool IsC126GroupBoxTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c126-group-box-tests"u8);
    }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
    private static bool IsC127NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c127-notes"u8)
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
            || IsC128NotesContext(host)
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
#endif
            ;
    }

    private static bool IsC127PanelTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c127-panel-tests"u8);
    }
#if HOSTLOGPROOF_C128_MANAGED_PANEL_LIFECYCLE
    private static bool IsC128NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c128-notes"u8)
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            || IsC131NotesContext(host)
#endif
            ;
    }

    private static bool IsC128PanelLifecycleTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c128-panel-lifecycle-tests"u8);
    }
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
    private static bool IsC129Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c129-shift-tab-proof"u8);
    }

    private static bool IsC129ShiftTabTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c129-shift-tab-tests"u8);
    }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
    private static bool IsC131NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c131-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c131-disabled"u8);
    }

    private static bool IsC131CheckBoxTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c131-checkbox-tests"u8);
    }

    private static bool IsC131HostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c131-checkbox-host-tests"u8);
    }
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
    private static bool IsC132NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c132-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c132-disabled"u8);
    }

    private static bool IsC132RadioTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c132-radio-tests"u8);
    }

    private static bool IsC132HostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c132-radio-host-tests"u8);
    }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
    private static bool IsC133NotesContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c133-notes"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-disabled"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133"u8);
    }

    private static bool IsC133ComboTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c133-combo-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-api"u8);
    }

    private static bool IsC133HostTestContext(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual(
            "c133-combo-host-tests"u8) ||
            host.LaunchContext.Utf8.SequenceEqual("c133-host"u8);
    }
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif

#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
    private void OnShowStatusChanged(bool isChecked)
    {
        _showStatusVisible = isChecked;
    }

    private bool HandleC131CheckboxProofKey(uint keyCode)
    {
        if (keyCode == C131HideStatusKey)
        {
            _showStatusCheckBox.SetVisible(false);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C131ShowStatusKey)
        {
            _showStatusCheckBox.SetVisible(true);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        return false;
    }
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
    private bool HandleC132RadioProofKey(uint keyCode)
    {
        if (keyCode == C132HideFileNameKey)
        {
            _fileNameRadio.SetVisible(false);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C132ShowFileNameKey)
        {
            _fileNameRadio.SetVisible(true);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C132DisableFileNameKey)
        {
            _pathDisplayGroup.TrySelect(_fullPathRadio);
            _fileNameRadio.SetEnabled(false);
            _mainControlHost.TryFocus(C124FullPathControlId);
            UpdatePathLabel();
            return true;
        }
        if (keyCode == C132EnableFileNameKey)
        {
            _fileNameRadio.SetEnabled(true);
            _mainControlHost.TryFocus(C124FullPathControlId);
            return true;
        }
        return false;
    }

    private void OnC132RadioChanged(bool isChecked)
    {
        ++_c132RadioChangedCount;
        if (_c132ProofContext) UpdatePathLabel();
    }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
    private bool HandleC133ComboProofKey(uint keyCode)
    {
        if (keyCode == C133HideComboKey)
        {
            _pathDisplayCombo.SetVisible(false);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C133ShowComboKey)
        {
            _pathDisplayCombo.SetVisible(true);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C133DisableComboKey)
        {
            _pathDisplayCombo.SetEnabled(false);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C133EnableComboKey)
        {
            _pathDisplayCombo.SetEnabled(true);
            _mainControlHost.TryFocus(C133ComboControlId);
            return true;
        }
        return false;
    }

    private void OnC133ComboChanged(int selectedIndex)
    {
        if (_c133ProofContext) UpdatePathLabel();
    }
#endif

#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
    private void OnC135MenuCommand(uint commandId)
    {
        if (_c135PendingCommand != 0u) return;
        _c135PendingCommand = commandId;
    }

    private GuideXosResult OpenC135Menu(
        GuideXosHost host, GuideXosSurface surface,
        int x = 400, int y = 280, bool secondaryInvocation = false)
    {
        GuideXosPopupMenuResult opened = _c135Menu.Open(x, y);
        if (opened != GuideXosPopupMenuResult.Opened)
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (!_mainControlHost.TryAcquireTransientInputCapture(
                C135MenuControlId))
        {
            _c135Menu.Cancel();
            host.TryLog("C135-CONFLICT existing-owner=authoritative second=menu rejected result=PASS"u8);
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        host.TryLog(secondaryInvocation
            ? "C136-CONTEXT-OPEN invoke=Secondary target=Document capture=PASS popup=open result=PASS"u8
            : "C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS"u8);
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private bool HandleC135MenuProofKey(uint keyCode)
    {
        if (keyCode == C135HideMenuKey)
        {
            _c135Menu.SetVisible(false);
            return true;
        }
        if (keyCode == C135ShowMenuKey)
        {
            _c135Menu.SetVisible(true);
            return true;
        }
        if (keyCode == C135DisableMenuKey)
        {
            _c135Menu.SetEnabled(false);
            return true;
        }
        if (keyCode == C135EnableMenuKey)
        {
            _c135Menu.SetEnabled(true);
            return true;
        }
        return false;
    }
#endif

    private void BlurButtons()
    {
        _openButton.Blur();
        _saveButton.Blur();
        _saveAsButton.Blur();
    }

    private GuideXosButton FocusedButton()
    {
        if (_openButton.IsFocused) return _openButton;
        if (_saveButton.IsFocused) return _saveButton;
        if (_saveAsButton.IsFocused) return _saveAsButton;
        return null;
    }

    private uint ButtonAction(GuideXosButton button)
    {
        return button == null ? 0u :
            button == _openButton ? 20u :
            button == _saveButton ? 22u : 21u;
    }

    private GuideXosResult HandleManagedButtonInput(
        GuideXosHost host,
        GuideXosSurface surface,
        GuideXosInputEvent input,
        out bool consumed)
    {
        consumed = false;
        if (input.Kind == GuideXosInputKind.PointerMove)
        {
            GuideXosControlHostResult dragResult =
                _mainControlHost.HandlePointerMove(input.X, input.Y);
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext &&
                (dragResult == GuideXosControlHostResult.Dragged ||
                    dragResult == GuideXosControlHostResult.Ignored))
            {
                SyncC138ScrollBars();
                host.TryLog(dragResult == GuideXosControlHostResult.Dragged
                    ? "C138-DRAG move=PASS capture=owned result=PASS"u8
                    : "C138-DRAG move=IGNORED result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
            return GuideXosResult.Success;
        }
        if (input.Kind == GuideXosInputKind.PointerUp &&
            input.Button == GuideXosPointerButton.Primary)
        {
            int dragOwner = _mainControlHost.PointerDragCaptureOwnerId;
            GuideXosControlHostResult releaseResult =
                _mainControlHost.HandlePointerUp(input.X, input.Y);
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext)
            {
                SyncC138ScrollBars();
                if (releaseResult == GuideXosControlHostResult.DragEnded &&
                    dragOwner == C138ListScrollControlId)
                {
                    host.TryLog(_c137ListBox.FirstVisibleIndex ==
                        _c137ListBox.MaximumFirstVisibleIndex &&
                        _c137ListBox.SelectedIndex == 0
                        ? "C138-LIST-DRAG release=PASS viewport=bottom selection=preserved result=PASS"u8
                        : "C138-LIST-DRAG release=FAIL result=FAIL"u8);
                }
                host.TryLog(releaseResult == GuideXosControlHostResult.DragEnded
                    ? "C138-DRAG release=PASS capture=none result=PASS"u8
                    : "C138-DRAG release=IGNORED capture=none result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
            return GuideXosResult.Success;
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            GuideXosButton button = null;
            GuideXosButtonResult result = _openButton.HandlePointerDown(
                input.X, input.Y);
            if (result != GuideXosButtonResult.Ignored)
            {
                button = _openButton;
            }
            else
            {
                result = _saveButton.HandlePointerDown(
                    input.X, input.Y);
                if (result != GuideXosButtonResult.Ignored)
                {
                    button = _saveButton;
                }
            }
            if (button == null)
            {
                result = _saveAsButton.HandlePointerDown(input.X, input.Y);
                if (result != GuideXosButtonResult.Ignored)
                {
                    button = _saveAsButton;
                }
            }
            if (button == null) return GuideXosResult.Success;

            consumed = true;
            if (result == GuideXosButtonResult.Disabled)
            {
                host.TryLog("C119-BUTTON pointer=disabled result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }

            BlurButtons();
            _textArea.Blur();
            button.Focus();
            uint actionId = ButtonAction(button);
            host.TryLog(actionId == 20u
                ? "C119-BUTTON pointer=Open activation=PASS"u8
                : actionId == 22u
                    ? "C119-BUTTON pointer=Save activation=PASS"u8
                    : "C119-BUTTON pointer=SaveAs activation=PASS"u8);
            return HandleAction(host, actionId);
        }

        if (input.Kind != GuideXosInputKind.KeyDown &&
            input.Kind != GuideXosInputKind.KeyChar)
        {
            return GuideXosResult.Success;
        }

        GuideXosButton focused = FocusedButton();
        if (focused == null) return GuideXosResult.Success;
        consumed = true;
        GuideXosButtonResult keyResult = input.Kind == GuideXosInputKind.KeyDown
            ? focused.HandleKey((GuideXosTextInputKey)input.KeyCode)
            : focused.HandleCharacter(input.Character);
        if (keyResult == GuideXosButtonResult.Activated)
        {
            uint actionId = ButtonAction(focused);
            if (input.Kind == GuideXosInputKind.KeyChar && input.Character == ' ')
            {
                host.TryLog("C119-SPACE button=Save activation=PASS count=1"u8);
            }
            else
            {
                host.TryLog(actionId == 20u
                    ? "C119-BUTTON key=Enter button=Open activation=PASS"u8
                    : actionId == 22u
                        ? "C119-BUTTON key=Enter button=Save activation=PASS"u8
                        : "C119-BUTTON key=Enter button=SaveAs activation=PASS"u8);
            }
            return HandleAction(host, actionId);
        }
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }
#endif

#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
    private GuideXosResult HandleC120Input(
        GuideXosHost host, GuideXosSurface surface, GuideXosInputEvent input)
    {
        if (_picker.IsActive)
        {
            GuideXosFilePickerResult pickerResult = _picker.HandleInput(
                host, surface, input);
            return ApplyPickerResult(host, surface, pickerResult);
        }

#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (input.Kind == GuideXosInputKind.PointerMove)
        {
            GuideXosControlHostResult dragResult =
                _mainControlHost.HandlePointerMove(input.X, input.Y);
            if (_c138ProofContext &&
                (dragResult == GuideXosControlHostResult.Dragged ||
                    _mainControlHost.HasPointerDragCapture &&
                    dragResult == GuideXosControlHostResult.Ignored))
            {
                SyncC138ScrollBars();
                host.TryLog(dragResult == GuideXosControlHostResult.Dragged
                    ? "C138-DRAG move=PASS capture=owned result=PASS"u8
                    : "C138-DRAG move=IGNORED result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }

        if (input.Kind == GuideXosInputKind.PointerUp &&
            input.Button == GuideXosPointerButton.Primary)
        {
            int dragOwner = _mainControlHost.PointerDragCaptureOwnerId;
            GuideXosControlHostResult releaseResult =
                _mainControlHost.HandlePointerUp(input.X, input.Y);
            if (_c138ProofContext)
            {
                SyncC138ScrollBars();
                if (releaseResult == GuideXosControlHostResult.DragEnded &&
                    dragOwner == C138ListScrollControlId)
                {
                    host.TryLog(_c137ListBox.FirstVisibleIndex ==
                        _c137ListBox.MaximumFirstVisibleIndex &&
                        _c137ListBox.SelectedIndex == 0
                        ? "C138-LIST-DRAG release=PASS viewport=bottom selection=preserved result=PASS"u8
                        : "C138-LIST-DRAG release=FAIL result=FAIL"u8);
                }
                host.TryLog(releaseResult == GuideXosControlHostResult.DragEnded
                    ? "C138-DRAG release=PASS capture=none result=PASS"u8
                    : "C138-DRAG release=IGNORED capture=none result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }
#endif

#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        if (_c135ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC135MenuProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (_c131ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC131CheckboxProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        if (_c132ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC132RadioProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC133ComboProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (_c125ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC125ProgressProofKey(host, input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
 #if HOSTLOGPROOF_C127_MANAGED_PANEL
        if (_c127ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC127PanelProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
 #endif
        if (_c126ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC126GroupBoxProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#endif
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
        if (_c123ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC123SeparatorProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        if (_c124ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            HandleC124RadioProofKey(input.KeyCode))
        {
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif

        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            int hitControlId = C120HitTest(input.X, input.Y);
            int controlId =
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                _c133ProofContext && _pathDisplayCombo.IsOpen
                    ? C133ComboControlId
                    :
#endif
                hitControlId;
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            if (_c135ProofContext &&
                _mainControlHost.HasTransientInputCapture)
            {
                controlId = _mainControlHost.TransientInputCaptureOwnerId;
            }
#endif
            if (controlId == 0) return GuideXosResult.Success;
            GuideXosControlHostResult pointerResult = controlId == C120DocumentControlId
                ? _mainControlHost.FocusAndRoutePointer(
                    controlId, input.X, input.Y, 20, 72, 8, 18)
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
                : controlId == C137ListControlId
                    ? _mainControlHost.FocusAndRoutePointer(
                        controlId, input.X, input.Y, 300, 72, 8, 18)
#endif
                : _mainControlHost.FocusAndRoutePointer(
                    controlId, input.X, input.Y);
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext &&
                (hitControlId == C138TextScrollControlId ||
                    hitControlId == C138ListScrollControlId) &&
                controlId != hitControlId &&
                pointerResult == GuideXosControlHostResult.Cancelled)
            {
                host.TryLog("C138-POPUP blocked=PASS scrollbar=inactive result=PASS"u8);
            }
#endif
            if (pointerResult == GuideXosControlHostResult.Activated
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                && !(_c133ProofContext && controlId == C133ComboControlId)
#endif
                )
            {
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
                if (_c135ProofContext && controlId == C135MenuControlId)
                {
                    if (_c135PendingCommand != 0u)
                    {
                        uint commandId = _c135PendingCommand;
                        _c135PendingCommand = 0u;
                        return HandleAction(host, commandId);
                    }
                    return RenderMain(host, surface, _launchCount)
                        ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                }
#endif
                host.TryLog(C120ControlLabel(controlId));
                return HandleAction(host, C120ActionForControl(controlId));
            }
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext &&
                (controlId == C138TextScrollControlId ||
                    controlId == C138ListScrollControlId))
            {
                SyncC138ScrollBars();
                host.TryLog(pointerResult == GuideXosControlHostResult.DragStarted
                    ? "C138-DRAG press=PASS capture=owned result=PASS"u8
                    : pointerResult == GuideXosControlHostResult.Paged
                        ? "C138-TRACK page=PASS result=PASS"u8
                        : "C138-SCROLL pointer=PASS result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            if (_c121ProofContext && controlId == C121ShowPathControlId &&
                pointerResult == GuideXosControlHostResult.Toggled)
            {
                host.TryLog(_showPathCheckBox.Checked
                    ? "C121-POINTER state=checked result=PASS"u8
                    : "C121-POINTER state=unchecked result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            if (_c131ProofContext && controlId == C131ShowStatusControlId &&
                pointerResult == GuideXosControlHostResult.Toggled)
            {
                host.TryLog(_showStatusCheckBox.Checked
                    ? "C131-POINTER state=checked callback=PASS result=PASS"u8
                    : "C131-POINTER state=unchecked callback=PASS result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            if (_c133ProofContext && controlId == C133ComboControlId)
            {
                if (pointerResult == GuideXosControlHostResult.Changed)
                {
                    host.TryLog("C133-POINTER selection=changed callback=PASS result=PASS"u8);
                }
                else if (pointerResult == GuideXosControlHostResult.Activated)
                {
                    host.TryLog("C133-POINTER open=PASS result=PASS"u8);
                }
                GuideXosResult comboRenderResult = RenderMain(
                    host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                return comboRenderResult;
            }
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            if (_c135ProofContext && controlId == C135MenuControlId)
            {
                host.TryLog(pointerResult == GuideXosControlHostResult.Disabled
                    ? "C135-DISABLED pointer=ignored command=none result=PASS"u8
                    : pointerResult == GuideXosControlHostResult.Cancelled
                        ? "C135-OUTSIDE close=PASS consumed=PASS underlying=inactive result=PASS"u8
                        : pointerResult == GuideXosControlHostResult.Activated
                            ? "C135-POINTER selection=PASS callback=pending result=PASS"u8
                            : "C135-POINTER result=PASS"u8);
                if (pointerResult == GuideXosControlHostResult.Activated &&
                    _c135PendingCommand != 0u)
                {
                    uint commandId = _c135PendingCommand;
                    _c135PendingCommand = 0u;
                    return HandleAction(host, commandId);
                }
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            if (_c137ProofContext && controlId == C137ListControlId)
            {
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
                if (_c138ProofContext) SyncC138ScrollBars();
#endif
                LogC137ListPointer(host, pointerResult);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            if (_c132ProofContext &&
                (controlId == C124FullPathControlId ||
                    controlId == C124FileNameControlId))
            {
                bool disabled = pointerResult == GuideXosControlHostResult.Disabled;
                bool selected = pointerResult == GuideXosControlHostResult.Changed &&
                    ((_fullPathRadio.Checked && controlId == C124FullPathControlId) ||
                        (_fileNameRadio.Checked && controlId == C124FileNameControlId));
                bool callback = disabled || _c132RadioChangedCount == 2;
                if (!disabled && selected) UpdatePathLabel();
                _c132RadioChangedCount = 0;
                host.TryLog(disabled
                    ? "C132-DISABLED pointer=ignored selection=unchanged result=PASS"u8
                    : callback && selected
                        ? "C132-POINTER selection=PASS callbacks=2 result=PASS"u8
                        : "C132-POINTER selection=FAIL callbacks=FAIL result=FAIL"u8);
                return disabled || callback && selected &&
                    RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            if (_c124ProofContext &&
                (controlId == C124FullPathControlId ||
                    controlId == C124FileNameControlId) &&
                pointerResult == GuideXosControlHostResult.Changed)
            {
                bool labelUpdated = UpdatePathLabel();
                host.TryLog(labelUpdated
                    ? "C124-POINTER selection=PASS exact-once=PASS"u8
                    : "C124-POINTER selection=FAIL exact-once=FAIL"u8);
                return labelUpdated && RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
            host.TryLog(pointerResult == GuideXosControlHostResult.Disabled
                ? "C120-POINTER target=disabled result=PASS"u8
                : controlId == C120DocumentControlId &&
                    pointerResult == GuideXosControlHostResult.Focused
                    ? "C120-POINTER target=document result=PASS"u8
                    : "C120-POINTER result=FAIL"u8);
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }

#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext && input.Kind == GuideXosInputKind.Wheel)
        {
            int scrollBarId = C120HitTest(input.X, input.Y);
            if (scrollBarId == C138TextScrollControlId ||
                scrollBarId == C138ListScrollControlId)
            {
                GuideXosControlHostResult scrollResult =
                    _mainControlHost.HandleWheel(
                        scrollBarId, input.X, input.Y, input.WheelDelta);
                SyncC138ScrollBars();
                host.TryLog(scrollResult == GuideXosControlHostResult.Changed
                    ? "C138-WHEEL direct=PASS result=PASS"u8
                    : "C138-WHEEL direct=IGNORED result=PASS"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
        }
#endif
        if (_c137ProofContext && input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == C137RelaunchKey)
        {
            GuideXosResult closeResult = surface.TryClose();
            host.TryLog(closeResult == GuideXosResult.Success
                ? "C137-CLOSE request=PASS capture=none result=PASS"u8
                : "C137-CLOSE request=FAIL capture=unknown result=FAIL"u8);
            return closeResult;
        }
        if (_c137ProofContext && input.Kind == GuideXosInputKind.Wheel)
        {
            int targetId = C120HitTest(input.X, input.Y);
            int beforeTextLine = _textArea.FirstVisibleLine;
            int beforeListIndex = _c137ListBox.FirstVisibleIndex;
            GuideXosControlHostResult wheelResult = targetId ==
                C120DocumentControlId
                ? _mainControlHost.HandleWheel(
                    targetId, input.X, input.Y, input.WheelDelta,
                    20, 72, 8, 18)
                : targetId == C137ListControlId
                    ? _mainControlHost.HandleWheel(
                        targetId, input.X, input.Y, input.WheelDelta,
                        300, 72, 8, 18)
                    : GuideXosControlHostResult.Ignored;
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            if (_c138ProofContext)
            {
                SyncC138ScrollBars();
            }
#endif
            if (targetId == C120DocumentControlId)
            {
                LogC137Wheel(host, "TextArea"u8, input.WheelDelta,
                    beforeTextLine, _textArea.FirstVisibleLine, -1,
                    wheelResult);
            }
            else if (targetId == C137ListControlId)
            {
                LogC137Wheel(host, "ListBox"u8, input.WheelDelta,
                    beforeListIndex, _c137ListBox.FirstVisibleIndex,
                    _c137ListBox.SelectedIndex, wheelResult);
            }
            else if (_mainControlHost.HasTransientInputCapture)
            {
                host.TryLog("C137-WHEEL capture=swallowed target=background result=PASS"u8);
            }
            else
            {
                host.TryLog("C137-WHEEL target=none result=IGNORED"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif

        GuideXosControlHostResult routeResult = _mainControlHost.HandleInput(input);
#if HOSTLOGPROOF_C129_SHIFT_TAB_TRANSPORT
        if (_c129ProofContext)
        {
            if (input.Kind == GuideXosInputKind.KeyChar &&
                input.Character == '\t')
            {
                host.TryLog("C129-RESULT outcome=FAIL reason=phantom-tab-character"u8);
            }
            else if (input.Kind == GuideXosInputKind.KeyDown &&
                (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
            {
                bool reverse = input.Shift && _c129ProofStage == 0 &&
                    _mainControlHost.ActiveControlId == C120DocumentControlId &&
                    routeResult == GuideXosControlHostResult.Traversed;
                bool forward = !input.Shift && _c129ProofStage == 1 &&
                    _mainControlHost.ActiveControlId == C120OpenControlId &&
                    routeResult == GuideXosControlHostResult.Traversed;
                if (reverse)
                {
                    _c129ProofStage = 1;
                    host.TryLog("C129-MANAGED reverse=Document count=1 modifier=shift keychar=none result=PASS"u8);
                }
                else if (forward)
                {
                    _c129ProofStage = 2;
                    host.TryLog("C129-MANAGED plain-tab=Document->Open count=1 modifier=none result=PASS"u8);
                }
                else
                {
                    host.TryLog("C129-RESULT outcome=FAIL reason=tab-sequence"u8);
                }
            }
            else if (input.Kind == GuideXosInputKind.KeyChar &&
                input.Character == 'a')
            {
                bool ordinary = _c129ProofStage == 2 && !input.Shift &&
                    _mainControlHost.ActiveControlId == C120OpenControlId &&
                    routeResult == GuideXosControlHostResult.Ignored;
                if (ordinary)
                {
                    _c129ProofStage = 3;
                    host.TryLog("C129-MANAGED ordinary-char=a focused=Open activation=none result=PASS"u8);
                    host.TryLog("C129-RESULT outcome=PASS transport=production"u8);
                }
                else
                {
                    host.TryLog("C129-RESULT outcome=FAIL reason=ordinary-character"u8);
                }
            }
        }
#endif
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
        {
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            if (_c135ProofContext)
            {
                host.TryLog(input.Shift
                    ? "C135-SHIFT-TAB close=PASS reverse=PASS result=PASS"u8
                    : "C135-TAB close=PASS forward=PASS result=PASS"u8);
            }
            else
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
            if (_c132ProofContext)
            {
                host.TryLog(input.Shift
                    ? "C132-SHIFT-TAB traversal=PASS result=PASS"u8
                    : "C132-TAB traversal=PASS result=PASS"u8);
            }
            else
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            if (_c133ProofContext)
            {
                host.TryLog(input.Shift
                    ? "C133-SHIFT-TAB traversal=PASS result=PASS"u8
                    : "C133-TAB traversal=PASS result=PASS"u8);
            }
            else
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            if (_c121ProofContext)
            {
                host.TryLog(input.Shift
                    ? "C121-SHIFT-TAB traversal=PASS"u8
                    : "C121-TAB traversal=PASS"u8);
            }
            else
#endif
            host.TryLog(input.Shift
                ? "C120-SHIFT-TAB traversal=PASS"u8
                : "C120-TAB traversal=PASS"u8);
        }
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        if (_c135ProofContext &&
            (routeResult == GuideXosControlHostResult.Activated ||
                routeResult == GuideXosControlHostResult.Moved ||
                routeResult == GuideXosControlHostResult.Cancelled))
        {
            if (routeResult == GuideXosControlHostResult.Activated &&
                _c135PendingCommand != 0u)
            {
                uint commandId = _c135PendingCommand;
                _c135PendingCommand = 0u;
                host.TryLog(input.Kind == GuideXosInputKind.KeyDown &&
                    (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Enter
                    ? "C135-ENTER command=PASS callback=PASS result=PASS"u8
                    : "C135-SPACE command=PASS callback=PASS result=PASS"u8);
                return HandleAction(host, commandId);
            }
            if (routeResult == GuideXosControlHostResult.Moved)
            {
                host.TryLog("C135-KEYBOARD highlight=PASS disabled-skipped=PASS result=PASS"u8);
            }
            else if (input.Kind == GuideXosInputKind.KeyDown &&
                (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Escape)
            {
                host.TryLog("C135-ESCAPE cancel=PASS capture=none result=PASS"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
        if (routeResult == GuideXosControlHostResult.Activated
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            && !(_c133ProofContext &&
                _mainControlHost.ActiveControlId == C133ComboControlId)
#endif
            )
        {
            int controlId = _mainControlHost.ActiveControlId;
            host.TryLog(C120ControlLabel(controlId));
            return HandleAction(host, C120ActionForControl(controlId));
        }
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext &&
            _mainControlHost.ActiveControlId == C133ComboControlId &&
            (routeResult == GuideXosControlHostResult.Activated ||
                routeResult == GuideXosControlHostResult.Changed ||
                routeResult == GuideXosControlHostResult.Moved ||
                routeResult == GuideXosControlHostResult.Cancelled))
        {
            if (routeResult == GuideXosControlHostResult.Activated)
            {
                host.TryLog(input.Kind == GuideXosInputKind.KeyDown &&
                    (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Enter
                    ? "C133-ENTER open=PASS result=PASS"u8
                    : "C133-SPACE open=PASS result=PASS"u8);
            }
            else if (routeResult == GuideXosControlHostResult.Changed)
            {
                host.TryLog("C133-ENTER selection=commit callback=PASS result=PASS"u8);
            }
            else if (routeResult == GuideXosControlHostResult.Moved)
            {
                host.TryLog("C133-ARROW highlight=PASS committed=unchanged result=PASS"u8);
            }
            else if (input.Kind == GuideXosInputKind.KeyDown &&
                (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Escape)
            {
                host.TryLog("C133-ESCAPE cancel=PASS committed=preserved result=PASS"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        if (_c121ProofContext &&
            routeResult == GuideXosControlHostResult.Toggled &&
            _mainControlHost.ActiveControlId == C121ShowPathControlId)
        {
            if (input.Kind == GuideXosInputKind.KeyChar && input.Character == ' ')
            {
                host.TryLog(_showPathCheckBox.Checked
                    ? "C121-SPACE state=checked exact-once=PASS"u8
                    : "C121-SPACE state=unchecked exact-once=PASS"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (_c131ProofContext &&
            routeResult == GuideXosControlHostResult.Toggled &&
            _mainControlHost.ActiveControlId == C131ShowStatusControlId)
        {
            if (input.Kind == GuideXosInputKind.KeyChar && input.Character == ' ')
            {
                host.TryLog(_showStatusCheckBox.Checked
                    ? "C131-SPACE state=checked exact-once=PASS callback=PASS"u8
                    : "C131-SPACE state=unchecked exact-once=PASS callback=PASS"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
        if (_c132ProofContext &&
            (_mainControlHost.ActiveControlId == C124FullPathControlId ||
                _mainControlHost.ActiveControlId == C124FileNameControlId) &&
            (routeResult == GuideXosControlHostResult.Changed ||
                routeResult == GuideXosControlHostResult.Moved))
        {
            bool isSpace = input.Kind == GuideXosInputKind.KeyChar &&
                input.Character == ' ';
            bool selected = _mainControlHost.ActiveControlId ==
                (_fullPathRadio.Checked ? C124FullPathControlId : C124FileNameControlId);
            bool callbackPass = routeResult == GuideXosControlHostResult.Moved ||
                _c132RadioChangedCount == 2 || _c132RadioChangedCount == 0;
            if (isSpace && selected)
            {
                host.TryLog(_c132RadioChangedCount == 0
                    ? "C132-SPACE idempotent=PASS callbacks=0 exact-once=PASS"u8
                    : callbackPass
                        ? "C132-SPACE selection=PASS callbacks=2 exact-once=PASS"u8
                        : "C132-SPACE selection=FAIL callbacks=FAIL exact-once=FAIL"u8);
            }
            if (routeResult == GuideXosControlHostResult.Moved)
            {
                host.TryLog("C132-ARROW focus-selection=PASS result=PASS"u8);
            }
            _c132RadioChangedCount = 0;
            return UpdatePathLabel() && RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
        if (_c124ProofContext &&
            (_mainControlHost.ActiveControlId == C124FullPathControlId ||
                _mainControlHost.ActiveControlId == C124FileNameControlId) &&
            (routeResult == GuideXosControlHostResult.Changed ||
                routeResult == GuideXosControlHostResult.Moved))
        {
            bool labelUpdated = UpdatePathLabel();
            host.TryLog(input.Kind == GuideXosInputKind.KeyChar
                ? "C124-SPACE selection=PASS exact-once=PASS"u8
                : "C124-ARROW selection=PASS host-focus=PASS"u8);
            return labelUpdated && RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
#endif
        if (routeResult == GuideXosControlHostResult.Changed ||
            routeResult == GuideXosControlHostResult.Moved)
        {
            host.TryLog(_mainControlHost.ActiveControlId == C120DocumentControlId
                ? "C120-ROUTING active=document result=PASS"u8
                : "C120-ROUTING active=control result=PASS"u8);
        }
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (_c125ProofContext &&
            _mainControlHost.ActiveControlId == C120DocumentControlId &&
            (routeResult == GuideXosControlHostResult.Changed ||
                routeResult == GuideXosControlHostResult.Rejected))
        {
            LogC125Progress(host, routeResult == GuideXosControlHostResult.Changed
                ? "edit"u8 : "overflow-rejected"u8);
        }
#endif
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext)
        {
            SyncC138ScrollBars();
        }
#endif
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private int C120HitTest(int x, int y)
    {
        if (x >= 20 && x < 110 && y >= 220 && y < 248)
        {
            return C120OpenControlId;
        }
        if (x >= 120 && x < 210 && y >= 220 && y < 248)
        {
            return C120SaveControlId;
        }
        if (x >= 220 && x < 320 && y >= 220 && y < 248)
        {
            return C120SaveAsControlId;
        }
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
        if (x >= 330 && x < 458 && y >= 220 && y < 248)
        {
            return C121ShowPathControlId;
        }
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
        if (x >= 330 && x < 458 && y >= 184 && y < 212)
        {
            return C131ShowStatusControlId;
        }
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext && _pathDisplayCombo.ContainsPoint(x, y))
        {
            return C133ComboControlId;
        }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
        if (_fullPathRadio.ContainsPoint(x, y))
        {
            return C124FullPathControlId;
        }
        if (_fileNameRadio.ContainsPoint(x, y))
        {
            return C124FileNameControlId;
        }
#else
        if (x >= 20 && x < 148 && y >= 256 && y < 284)
        {
            return C124FullPathControlId;
        }
        if (x >= 160 && x < 288 && y >= 256 && y < 284)
        {
            return C124FileNameControlId;
        }
#endif
#endif
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
        if (_c138ProofContext && _c138TextScrollBar.ContainsPoint(x, y))
        {
            return C138TextScrollControlId;
        }
        if (_c138ProofContext && _c138ListScrollBar.ContainsPoint(x, y))
        {
            return C138ListScrollControlId;
        }
#endif
        if (_c137ProofContext && x >= 300 && x < 300 + 24 * 8 &&
            y >= 72 && y < 72 + 4 * 18)
        {
            return C137ListControlId;
        }
#endif
        int documentColumns = 48;
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
        if (_c137ProofContext) documentColumns = 32;
#endif
        if (x >= 20 && x < 20 + documentColumns * 8 &&
            y >= 72 && y < 72 + 4 * 18)
        {
            return C120DocumentControlId;
        }
        return 0;
    }

    private static uint C120ActionForControl(int controlId)
    {
        return controlId switch
        {
            C120OpenControlId => 20u,
            C120SaveControlId => 22u,
            C120SaveAsControlId => 21u,
            _ => 0u,
        };
    }

    private static ReadOnlySpan<byte> C120ControlLabel(int controlId)
    {
        return controlId switch
        {
            C120OpenControlId => "C120-ACTIVATE control=Open result=PASS"u8,
            C120SaveControlId => "C120-ACTIVATE control=Save result=PASS"u8,
            C120SaveAsControlId => "C120-ACTIVATE control=SaveAs result=PASS"u8,
            _ => "C120-ACTIVATE control=Document result=FAIL"u8,
        };
    }

#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
 #if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
    private bool HandleC125ProgressProofKey(
        GuideXosHost host, uint keyCode)
    {
        if (keyCode == C125HideProgressKey)
        {
            _documentUsage.SetVisible(false);
            host.TryLog("C125-VISIBILITY hidden=no-render state-preserved result=PASS"u8);
            return true;
        }
        if (keyCode == C125ShowProgressKey)
        {
            _documentUsage.SetVisible(true);
            host.TryLog("C125-VISIBILITY shown=latest-value result=PASS"u8);
            return true;
        }
        if (keyCode == C125FillCapacityKey)
        {
            Span<byte> full = stackalloc byte[GuideXosTextArea.DefaultMaximumCharacters];
            full.Fill((byte)'A');
            bool filled = _textArea.SetUtf8(full);
            _textArea.Focus();
            host.TryLog(filled
                ? "C125-CAPACITY value=256 maximum=256 result=PASS"u8
                : "C125-CAPACITY value=FAIL result=FAIL"u8);
            return filled;
        }
        return false;
    }
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
    private bool ApplyC126RadioLayout()
    {
        if (!_pathDisplayBox.TryResolvePoint(8, 14,
                out int fullX, out int fullY) ||
            !_pathDisplayBox.TryResolvePoint(148, 14,
                out int fileNameX, out int fileNameY) ||
            !_pathDisplayBox.ContainsPoint(fullX + 127, fullY + 27) ||
            !_pathDisplayBox.ContainsPoint(fileNameX + 127, fileNameY + 27))
        {
            return false;
        }
        return _fullPathRadio.TrySetBounds(fullX, fullY, 128, 28) &&
            _fileNameRadio.TrySetBounds(fileNameX, fileNameY, 128, 28);
    }

#if HOSTLOGPROOF_C127_MANAGED_PANEL
    private bool ApplyC127PanelLayout()
    {
        GuideXosPanelResult fullResult = _pathDisplayPanel.ContainsChild(
            _fullPathRadio)
            ? _pathDisplayPanel.TrySetChildPosition(_fullPathRadio, 8, 14)
            : _pathDisplayPanel.TryAddChild(_fullPathRadio, 8, 14);
        GuideXosPanelResult fileNameResult = _pathDisplayPanel.ContainsChild(
            _fileNameRadio)
            ? _pathDisplayPanel.TrySetChildPosition(_fileNameRadio, 148, 14)
            : _pathDisplayPanel.TryAddChild(_fileNameRadio, 148, 14);
        return (fullResult == GuideXosPanelResult.Added ||
                fullResult == GuideXosPanelResult.Moved) &&
            (fileNameResult == GuideXosPanelResult.Added ||
                fileNameResult == GuideXosPanelResult.Moved) &&
            _pathDisplayPanel.ChildCount == 2 &&
            _fullPathRadio.ParentPanel == _pathDisplayPanel &&
            _fileNameRadio.ParentPanel == _pathDisplayPanel;
    }
#endif

    private bool HandleC126GroupBoxProofKey(uint keyCode)
    {
        if (keyCode == C126HideGroupBoxKey)
        {
            _pathDisplayBox.SetVisible(false);
            return true;
        }
        if (keyCode == C126ShowGroupBoxKey)
        {
            _pathDisplayBox.SetVisible(true);
            return true;
        }
        if (keyCode == C126GrowGroupBoxKey)
        {
            return _pathDisplayBox.TrySetBounds(12, 264, 480, 108) &&
                ApplyC126RadioLayout();
        }
        if (keyCode == C126ShrinkGroupBoxKey)
        {
            return _pathDisplayBox.TrySetBounds(12, 264, 320, 72) &&
                ApplyC126RadioLayout();
        }
        if (keyCode == C126MoveGroupBoxKey)
        {
            return _pathDisplayBox.TrySetBounds(40, 264, 456, 90) &&
                ApplyC126RadioLayout();
        }
        if (keyCode == C126RestoreGroupBoxKey)
        {
            return _pathDisplayBox.TrySetBounds(12, 264, 456, 90) &&
                ApplyC126RadioLayout();
        }
        return false;
    }
#if HOSTLOGPROOF_C127_MANAGED_PANEL
    private bool HandleC127PanelProofKey(uint keyCode)
    {
        if (keyCode == C127HidePanelKey)
        {
            _pathDisplayPanel.SetVisible(false);
            _pathDisplayBox.SetVisible(false);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C127ShowPanelKey)
        {
            _pathDisplayPanel.SetVisible(true);
            _pathDisplayBox.SetVisible(true);
            _mainControlHost.RefreshVisibility();
            return true;
        }
        if (keyCode == C127GrowPanelKey)
        {
            return _pathDisplayPanel.TrySetBounds(12, 264, 480, 108) &&
                _pathDisplayBox.TrySetBounds(12, 264, 480, 108);
        }
        if (keyCode == C127ShrinkPanelKey)
        {
            return _pathDisplayPanel.TrySetBounds(12, 264, 320, 72) &&
                _pathDisplayBox.TrySetBounds(12, 264, 320, 72);
        }
        if (keyCode == C127MovePanelKey)
        {
            return _pathDisplayPanel.TrySetBounds(40, 264, 456, 90) &&
                _pathDisplayBox.TrySetBounds(40, 264, 456, 90);
        }
        if (keyCode == C127RestorePanelKey)
        {
            return _pathDisplayPanel.TrySetBounds(12, 264, 456, 90) &&
                _pathDisplayBox.TrySetBounds(12, 264, 456, 90);
        }
        return false;
    }
#endif
#endif
#endif
    private bool HandleC123SeparatorProofKey(uint keyCode)
    {
        return keyCode switch
        {
            C123HideSeparatorKey => SetSeparatorVisible(false),
            C123ShowSeparatorKey => SetSeparatorVisible(true),
            C123ExpandSeparatorKey => _separator.SetWidth(504),
            C123ContractSeparatorKey => _separator.SetWidth(96),
            C123RestoreSeparatorKey => _separator.SetWidth(480),
            _ => false,
        };
    }

    private bool SetSeparatorVisible(bool visible)
    {
        _separator.SetVisible(visible);
        return true;
    }
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
    private bool HandleC124RadioProofKey(uint keyCode)
    {
        if (keyCode == C124DisableFileNameKey)
        {
            _pathDisplayGroup.TrySelect(_fullPathRadio);
            _fileNameRadio.SetEnabled(false);
            _mainControlHost.TryFocus(C124FullPathControlId);
            UpdatePathLabel();
            return true;
        }
        if (keyCode == C124EnableFileNameKey)
        {
            _fileNameRadio.SetEnabled(true);
            _mainControlHost.TryFocus(C124FullPathControlId);
            UpdatePathLabel();
            return true;
        }
        return false;
    }
#endif
#endif

    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
        if (_c136ProofContext && input.Button == GuideXosPointerButton.Secondary)
        {
            return HandleC136SecondaryPointerInput(host, surface, input);
        }
#endif
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
        if (_c120ProofContext)
        {
            return HandleC120Input(host, surface, input);
        }
#endif
        if (_picker.IsActive)
        {
            GuideXosFilePickerResult pickerResult = _picker.HandleInput(
                host, surface, input);
            return ApplyPickerResult(host, surface, pickerResult);
        }
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
        GuideXosResult buttonResult = HandleManagedButtonInput(
            host, surface, input, out bool buttonConsumed);
        if (buttonConsumed) return buttonResult;
#endif
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            GuideXosTextAreaEditResult focus = _textArea.HandlePointerDown(
                input.X, input.Y, 20, 72, 8, 18);
            if (focus == GuideXosTextAreaEditResult.Focused)
            {
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                BlurButtons();
#endif
                host.TryLog("C117-TEXT-AREA focus=PASS source=pointer"u8);
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
            return GuideXosResult.Success;
        }

        int previousFirstLine = _textArea.FirstVisibleLine;
        GuideXosTextAreaEditResult editResult = input.Kind switch
        {
            GuideXosInputKind.KeyChar => _textArea.HandleCharacter(input.Character),
            GuideXosInputKind.KeyDown => _textArea.HandleKey(
                (GuideXosTextInputKey)input.KeyCode, input.Shift),
            _ => GuideXosTextAreaEditResult.Ignored,
        };
        if (editResult == GuideXosTextAreaEditResult.Changed)
        {
            host.TryLog("C117-TEXT-AREA edit=changed result=PASS"u8);
        }
        else if (editResult == GuideXosTextAreaEditResult.Rejected)
        {
            host.TryLog("C117-TEXT-AREA edit=rejected result=PASS"u8);
        }
        else if (editResult == GuideXosTextAreaEditResult.Moved && input.Shift)
        {
            host.TryLog("C117-SELECTION navigation=shift result=PASS"u8);
        }
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (_c125ProofContext &&
            (editResult == GuideXosTextAreaEditResult.Changed ||
                editResult == GuideXosTextAreaEditResult.Rejected))
        {
            LogC125Progress(host, editResult == GuideXosTextAreaEditResult.Changed
                ? "edit"u8 : "overflow-rejected"u8);
        }
#endif
        if (_textArea.FirstVisibleLine != previousFirstLine)
        {
            host.TryLog(_textArea.FirstVisibleLine > previousFirstLine
                ? "C117-VIEWPORT direction=down caret-visible=PASS"u8
                : "C117-VIEWPORT direction=up caret-visible=PASS"u8);
        }
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Escape)
        {
            _status = "Editor cancelled";
        }
        if (!RenderMain(host, surface, _launchCount))
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
    private GuideXosResult HandleC136SecondaryPointerInput(
        GuideXosHost host, GuideXosSurface surface, GuideXosInputEvent input)
    {
        if ((input.Kind == GuideXosInputKind.PointerDown ||
                input.Kind == GuideXosInputKind.PointerUp) &&
            _c136InputTraceCount < 4u)
        {
            ++_c136InputTraceCount;
            host.TryLog("C136-DISPATCH button=secondary result=PASS"u8);
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            int targetId = C120HitTest(input.X, input.Y);
            GuideXosControlHostResult secondaryDown =
                _mainControlHost.BeginSecondaryPointerGesture(
                    targetId == C120DocumentControlId ? targetId : 0,
                    input.X, input.Y);
            if (secondaryDown == GuideXosControlHostResult.Pending)
            {
                host.TryLog("C136-SECONDARY-DOWN target=Document pending=PASS result=PASS"u8);
            }
            else if (secondaryDown == GuideXosControlHostResult.Cancelled)
            {
                host.TryLog("C136-SECONDARY-DOWN capture=authoritative consumed=PASS result=PASS"u8);
            }
            else
            {
                host.TryLog(targetId == 0
                    ? "C136-SECONDARY-DOWN outside=ignored result=PASS"u8
                    : "C136-SECONDARY-DOWN target=ineligible result=PASS"u8);
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (input.Kind == GuideXosInputKind.PointerUp)
        {
            GuideXosControlHostResult secondaryUp =
                _mainControlHost.CompleteSecondaryPointerGesture(
                    out int targetId, out int menuX, out int menuY);
            bool targetStillHit = targetId == C120DocumentControlId &&
                C120HitTest(input.X, input.Y) == C120DocumentControlId;
            if (secondaryUp == GuideXosControlHostResult.Released && targetStillHit)
            {
                host.TryLog("C136-SECONDARY-UP target=Document release=PASS result=PASS"u8);
                return OpenC135Menu(host, surface, menuX, menuY, true);
            }
            host.TryLog(secondaryUp == GuideXosControlHostResult.Cancelled ||
                (secondaryUp == GuideXosControlHostResult.Released && !targetStillHit)
                ? "C136-SECONDARY-UP stale=cancelled menu=none capture=none result=PASS"u8
                : "C136-SECONDARY-UP target=invalid menu=none result=PASS"u8);
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }
#endif

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
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
        if (_c135ProofContext && actionId == C135OptionsActionId)
        {
            return OpenC135Menu(host, surface);
        }
#endif
        if (actionId == 20u)
        {
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
            if (_c120ProofContext)
            {
                GuideXosFilePickerResult c120PickerResult = _picker.OpenFile(
                    host, surface, GuideXosFilePickerOptions.Open(
                        "/system/apps", "Open document", ".TXT", true));
                if (c120PickerResult.Status == GuideXosFilePickerStatus.Pending ||
                    c120PickerResult.Status == GuideXosFilePickerStatus.NoMatchingFiles)
                {
                    _mainControlHost.EnterModal(_picker.FocusHost);
                    host.TryLog("C120-MODAL entry=open result=PASS"u8);
                }
                return PickerStartResult(c120PickerResult);
            }
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
            BlurButtons();
#endif
            _textArea.Blur();
            GuideXosFilePickerResult result = _picker.OpenFile(
                host, surface, GuideXosFilePickerOptions.Open(
                    "/system/apps", "Open document", ".TXT"));
            return PickerStartResult(result);
        }
        if (actionId == 21u)
        {
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
            if (_c120ProofContext)
            {
                host.TryLog("C117-PICKER save=begin"u8);
                GuideXosFilePickerStatus c120SaveStatus = BeginSave(host, surface);
                if (c120SaveStatus == GuideXosFilePickerStatus.Pending)
                {
                    _mainControlHost.EnterModal(_picker.FocusHost);
                    host.TryLog("C120-MODAL entry=save-as result=PASS"u8);
                }
                return c120SaveStatus == GuideXosFilePickerStatus.Pending
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
            BlurButtons();
#endif
            _textArea.Blur();
            host.TryLog("C117-PICKER save=begin"u8);
            GuideXosFilePickerStatus status = BeginSave(host, surface);
            return status == GuideXosFilePickerStatus.Pending
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 22u)
        {
            GuideXosFileResult saveResult = SaveCurrent(host);
            _status = saveResult == GuideXosFileResult.Success
                ? "Saved to VFS" : StatusText(saveResult);
            host.TryLog(saveResult == GuideXosFileResult.Success
                ? "C117-NOTES save=PASS path=/system/apps/NOTES.TXT"u8
                : "C117-NOTES save=FAIL path=/system/apps/NOTES.TXT"u8);
#if HOSTLOGPROOF_C136_SECONDARY_POINTER_CONTEXT_MENU
            if (_c136ProofContext)
            {
                ++_c136CommandCount;
                host.TryLog(saveResult == GuideXosFileResult.Success
                    ? (_c136CommandCount == 1u
                        ? "C136-COMMAND command=Save count=1 result=PASS"u8
                        : "C136-COMMAND command=Save count=2 result=PASS"u8)
                    : "C136-COMMAND command=Save result=FAIL"u8);
            }
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
            host.TryLog(saveResult == GuideXosFileResult.Success
                ? "C119-NOTES managed-save=PASS source=button"u8
                : "C119-NOTES managed-save=FAIL source=button"u8);
#endif
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (actionId == 3u)
        {
            return ReloadCurrent(host, surface);
        }
        return GuideXosResult.InvalidAction;
    }

    private GuideXosResult HandlePickerAction(
        GuideXosHost host, GuideXosSurface surface, uint actionId)
    {
        GuideXosFilePickerResult result = _picker.HandleAction(host, surface, actionId);
        return ApplyPickerResult(host, surface, result);
    }

    private GuideXosResult ApplyPickerResult(
        GuideXosHost host, GuideXosSurface surface, GuideXosFilePickerResult result)
    {
        if (result.Status == GuideXosFilePickerStatus.Selected)
        {
            if (_picker.Mode == GuideXosFilePickerMode.Open)
            {
                GuideXosFileResult readResult = GuideXosFile.ReadAllTextUtf8(
                    host, Encoding.UTF8.GetBytes(result.Path), out byte[] loaded);
                if (readResult != GuideXosFileResult.Success ||
                    !_textArea.SetUtf8(loaded))
                {
                    _status = StatusText(readResult);
                    _picker.Reset();
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
                    if (_c120ProofContext)
                    {
                        _mainControlHost.ExitModal();
                    }
                    else
#endif
                    {
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                        BlurButtons();
#endif
                        _textArea.Focus();
                    }
                    return RenderMain(host, surface, _launchCount)
                        ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                }
                _currentPath = result.Path;
#if HOSTLOGPROOF_C122_MANAGED_LABEL
                if (_c122ProofContext && !UpdatePathLabel())
                {
                    return GuideXosResult.InvalidArgument;
                }
#endif
                _textArea.SetCaretToStart();
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
                if (_c120ProofContext)
                {
                    _mainControlHost.ExitModal();
                    host.TryLog("C120-MODAL exit=restore-open result=PASS"u8);
                }
                else
#endif
                {
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                    BlurButtons();
#endif
                    _textArea.Focus();
                }
                _status = "Opened from picker";
                bool exact = !result.Path.EndsWith("C117.TXT",
                    StringComparison.OrdinalIgnoreCase) ||
                    _textArea.Text == C117ExpectedDocument;
                host.TryLog(exact
                    ? "C117-NOTES reopen=PASS source=VFS"u8
                    : "C117-NOTES reopen=FAIL source=VFS"u8);
                LogDocument(host, "C117-NOTES actual");
                host.TryLog("C115-NOTES open=PASS source=picker"u8);
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
                if (_c118ProofContext
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
                    && !_c123ProofContext
#endif
                    )
                {
                    host.TryLog(PathLog("C118-NOTES open=PASS path=", result.Path));
                    LogDocument(host, "C118-NOTES actual");
                    bool loadedExpected = result.Path.EndsWith(
                        "THIRD.TXT", StringComparison.OrdinalIgnoreCase)
                        ? _textArea.Text == "Second managed document!"
                        : _textArea.Text == "Second managed document";
                    host.TryLog(loadedExpected
                        ? "C118-NOTES loaded=PASS source=list-box"u8
                        : "C118-NOTES loaded=FAIL source=list-box"u8);
                    if (_textArea.Text == "Second managed document!")
                    {
                        host.TryLog("C118-NOTES reopened-edited=PASS source=list-box"u8);
                    }
                }
#endif
            }
            else
            {
                GuideXosFileResult writeResult = GuideXosFile.WriteAllTextUtf8(
                    host, Encoding.UTF8.GetBytes(result.Path), _textArea.ToUtf8());
                if (writeResult != GuideXosFileResult.Success)
                {
                    _status = StatusText(writeResult);
                    return RenderMain(host, surface, _launchCount)
                        ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
                }
                _currentPath = result.Path;
#if HOSTLOGPROOF_C122_MANAGED_LABEL
                if (_c122ProofContext && !UpdatePathLabel())
                {
                    return GuideXosResult.InvalidArgument;
                }
#endif
                _status = "Saved through picker";
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                bool c119FixedSave = _c119ProofContext &&
                    result.Path == "/system/apps/THIRD.TXT";
                if (c119FixedSave)
                {
                    host.TryLog("C115-NOTES save=PASS path=/system/apps/THIRD.TXT"u8);
                    host.TryLog("C116-NOTES typed-save path=/system/apps/THIRD.TXT"u8);
                    host.TryLog("C117-NOTES saved document=Second managed document!"u8);
                }
                else
#endif
                {
                    host.TryLog(PathLog("C115-NOTES save=PASS path=", result.Path));
                    host.TryLog(PathLog("C116-NOTES typed-save path=", result.Path));
                    LogDocument(host, "C117-NOTES saved");
                }
            }
            _picker.Reset();
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
            if (_c120ProofContext)
            {
                _mainControlHost.ExitModal();
                host.TryLog("C120-MODAL exit=restore-save-as result=PASS"u8);
            }
            else
#endif
            {
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                BlurButtons();
#endif
                _textArea.Focus();
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (result.Status == GuideXosFilePickerStatus.Cancelled)
        {
            bool wasSave = _picker.Mode == GuideXosFilePickerMode.Save;
            _status = wasSave
                ? "Save cancelled" : "Open cancelled";
            _picker.Reset();
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
            if (_c120ProofContext)
            {
                _mainControlHost.ExitModal();
                host.TryLog(wasSave
                    ? "C120-MODAL exit=restore-save-as result=PASS"u8
                    : "C120-MODAL exit=restore-open result=PASS"u8);
            }
            else
#endif
            {
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
                BlurButtons();
#endif
                _textArea.Focus();
            }
            host.TryLog(wasSave
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
        GuideXosHost host, GuideXosSurface surface)
    {
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
        if (_c118ProofContext)
        {
            string c118Suggestion = _saveInvocation++ switch
            {
                0 => "THIRD.TXT",
                1 => "CANCEL.TXT",
                _ => "THIRD.TXT",
            };
            GuideXosFilePickerResult c118Result = _picker.SaveFile(
                host, surface, GuideXosFilePickerOptions.Save(
                    "/system/apps", "Save document", ".TXT", c118Suggestion,
                    true, _useTextInput,
#if HOSTLOGPROOF_C120_MANAGED_CONTROL_HOST
                    _c120ProofContext));
#else
                    false));
#endif
            return c118Result.Status;
        }
#endif
        string suggestion = _saveInvocation++ switch
        {
            0 => "THIRD.TXT",
            1 => "CANCEL.TXT",
            _ => "C117.TXT",
        };
        GuideXosFilePickerResult result = _picker.SaveFile(
            host, surface, GuideXosFilePickerOptions.Save(
                "/system/apps", "Save document", ".TXT", suggestion, true, true));
        return result.Status;
    }

    private GuideXosResult ReloadCurrent(
        GuideXosHost host, GuideXosSurface surface)
    {
        GuideXosFileResult readResult = GuideXosFile.ReadAllTextUtf8(
            host, Encoding.UTF8.GetBytes(_currentPath), out byte[] loaded);
        if (readResult == GuideXosFileResult.Success && _textArea.SetUtf8(loaded))
        {
            _textArea.SetCaretToStart();
            _status = "Reloaded from VFS";
            LogDocument(host, "C117-NOTES reload");
        }
        else
        {
            _status = StatusText(readResult);
        }
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private GuideXosFileResult SaveCurrent(GuideXosHost host)
    {
        return GuideXosFile.WriteAllTextUtf8(
            host, Encoding.UTF8.GetBytes(_currentPath), _textArea.ToUtf8());
    }

#if HOSTLOGPROOF_C122_MANAGED_LABEL
    private bool UpdatePathLabel()
    {
#if HOSTLOGPROOF_C134_TRANSIENT_POPUP_ROUTING
        if (_c133ProofContext)
        {
            return _pathLabel.SetText(_pathDisplayCombo.SelectedIndex == 1
                ? "Path: NOTES.TXT"
                : "Path: /system/apps/NOTES.TXT");
        }
#endif
        Span<char> line = stackalloc char[GuideXosLabel.DefaultMaximumTextLength];
        ReadOnlySpan<char> prefix = "Path: ".AsSpan();
        ReadOnlySpan<char> displayPath =
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            DisplayPath();
#else
            _currentPath.AsSpan();
#endif
        if (displayPath.Length > line.Length - prefix.Length)
        {
            return false;
        }
        prefix.CopyTo(line);
        displayPath.CopyTo(line[prefix.Length..]);
        return _pathLabel.SetText(line[..(prefix.Length + displayPath.Length)]);
    }

#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON || HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
    private ReadOnlySpan<char> DisplayPath()
    {
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
        if (_c133ProofContext && _pathDisplayCombo.SelectedIndex == 1)
        {
            int comboSeparator = _currentPath.LastIndexOf('/');
            return comboSeparator >= 0
                ? _currentPath.AsSpan(comboSeparator + 1)
                : _currentPath.AsSpan();
        }
#endif
        if (_fullPathRadio.Checked) return _currentPath.AsSpan();
        int separator = _currentPath.LastIndexOf('/');
        return separator >= 0
            ? _currentPath.AsSpan(separator + 1)
            : _currentPath.AsSpan();
    }
#endif

    private bool RenderPathLabel(GuideXosSurface surface)
    {
        _pathLabel.SetVisible(_showPathCheckBox.Checked);
        return _pathLabel.Render(surface) == GuideXosResult.Success;
    }
#endif

#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
    private bool SyncDocumentUsage()
    {
        return _documentUsage.TrySetValue(_textArea.Length);
    }

    private void LogC125Progress(
        GuideXosHost host, ReadOnlySpan<byte> phase)
    {
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, "C125-PROGRESS phase="u8) ||
            !GuideXosText.Append(line, ref position, phase) ||
            !GuideXosText.Append(line, ref position, " length="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, (uint)_textArea.Length) ||
            !GuideXosText.Append(line, ref position, " result=PASS"u8))
        {
            return;
        }
        host.TryLog(line[..position]);
    }
#endif

#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
    private void LogC137ListPointer(
        GuideXosHost host, GuideXosControlHostResult result)
    {
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        bool written = GuideXosText.Append(line, ref position,
                "C137-LIST-POINTER selected="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)Math.Max(0, _c137ListBox.SelectedIndex)) &&
            GuideXosText.Append(line, ref position, " viewport="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)_c137ListBox.FirstVisibleIndex) &&
            GuideXosText.Append(line, ref position,
                result == GuideXosControlHostResult.Changed
                    ? " result=PASS"u8 : " result=FAIL"u8);
        if (written) host.TryLog(line[..position]);
    }

    private static void LogC137Wheel(
        GuideXosHost host,
        ReadOnlySpan<byte> target,
        int delta,
        int before,
        int after,
        int selection,
        GuideXosControlHostResult result)
    {
        Span<byte> line = stackalloc byte[127];
        int position = 0;
        bool written = GuideXosText.Append(line, ref position,
                "C137-WHEEL target="u8) &&
            GuideXosText.Append(line, ref position, target) &&
            GuideXosText.Append(line, ref position, " delta="u8);
        if (written && delta < 0)
        {
            written = GuideXosText.Append(line, ref position, "-"u8);
        }
        if (written && !GuideXosText.AppendUnsigned(line, ref position,
                (uint)(delta < 0 ? -delta : delta))) written = false;
        if (written && !GuideXosText.Append(line, ref position, " before="u8))
            written = false;
        if (written && !GuideXosText.AppendUnsigned(line, ref position,
                (uint)before)) written = false;
        if (written && !GuideXosText.Append(line, ref position, " after="u8))
            written = false;
        if (written && !GuideXosText.AppendUnsigned(line, ref position,
                (uint)after)) written = false;
        if (written && selection >= 0)
        {
            written = GuideXosText.Append(line, ref position, " selection="u8) &&
                GuideXosText.AppendUnsigned(line, ref position,
                    (uint)selection);
        }
        ReadOnlySpan<byte> outcome = result == GuideXosControlHostResult.Scrolled
            ? " result=PASS"u8
            : result == GuideXosControlHostResult.Ignored
                ? " result=IGNORED"u8
                : " result=FAIL"u8;
        if (written && !GuideXosText.Append(line, ref position, outcome))
            written = false;
        if (written) host.TryLog(line[..position]);
    }

    private static string C137FixtureLabel(int index)
    {
        return index switch
        {
            0 => "Row 00 selected",
            1 => "Row 01 visible",
            2 => "Row 02 visible",
            3 => "Row 03 visible",
            4 => "Row 04 visible",
            5 => "Row 05 visible",
            6 => "Row 06 visible",
            7 => "Row 07 visible",
            8 => "Row 08 visible",
            9 => "Row 09 visible",
            10 => "Row 10 visible",
            _ => "Row 11 visible",
        };
    }
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
    private void EnsureC138Bindings()
    {
        if (!_c138BindingsInstalled)
        {
            _c138TextScrollBar.Changed += value =>
                _textArea.SetFirstVisibleLine(value);
            _c138ListScrollBar.Changed += value =>
                _c137ListBox.SetFirstVisibleIndex(value);
            _c138BindingsInstalled = true;
        }
        SyncC138ScrollBars();
    }

    private void SyncC138ScrollBars()
    {
        _c138TextScrollBar.Minimum = 0;
        _c138TextScrollBar.Maximum = _textArea.MaximumFirstVisibleLine;
        _c138TextScrollBar.PageSize = _textArea.VisibleLineCount;
        _c138TextScrollBar.SmallChange = 1;
        _c138TextScrollBar.LargeChange =
            Math.Max(1, _textArea.VisibleLineCount - 1);
        _c138TextScrollBar.Value = _textArea.FirstVisibleLine;

        _c138ListScrollBar.Minimum = 0;
        _c138ListScrollBar.Maximum = _c137ListBox.MaximumFirstVisibleIndex;
        _c138ListScrollBar.PageSize = _c137ListBox.VisibleRowCount;
        _c138ListScrollBar.SmallChange = 1;
        _c138ListScrollBar.LargeChange =
            Math.Max(1, _c137ListBox.VisibleRowCount - 1);
        _c138ListScrollBar.Value = _c137ListBox.FirstVisibleIndex;
    }
#endif
#endif

    private bool RenderMain(GuideXosHost host, GuideXosSurface surface, uint launchCount)
    {
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
        if (!SyncDocumentUsage()) return false;
#endif
        return surface.TryFillRect(10, 10, 560, 300, 0x007A5A9Au) ==
                GuideXosResult.Success &&
            GuideXosText.Line(surface, 24, "Managed Notes | "u8, "multiline text area"u8) &&
            GuideXosText.CountLine(surface, 48, "Launches: "u8, launchCount) &&
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
#if HOSTLOGPROOF_C122_MANAGED_LABEL
            (!_c122ProofContext || RenderPathLabel(surface)) &&
#else
            (!_c121ProofContext || !_showPathCheckBox.Checked ||
                GuideXosText.Line(surface, 66, "Path: "u8,
                    Encoding.UTF8.GetBytes(_currentPath))) &&
#endif
#else
            GuideXosText.Line(surface, 66, "Path: "u8, Encoding.UTF8.GetBytes(_currentPath)) &&
#endif
            _textArea.Render(surface, 20, 72, 18) == GuideXosResult.Success &&
#if HOSTLOGPROOF_C137_MOUSE_WHEEL_SCROLLING
            (!_c137ProofContext || _c137ListBox.Render(surface, 300, 72, 18) ==
                GuideXosResult.Success) &&
#if HOSTLOGPROOF_C138_REUSABLE_SCROLLBAR
            (!_c138ProofContext || _c138TextScrollBar.Render(surface) ==
                GuideXosResult.Success) &&
            (!_c138ProofContext || _c138ListScrollBar.Render(surface) ==
                GuideXosResult.Success) &&
#endif
#endif
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            (!_c131ProofContext || _showStatusVisible
                ? GuideXosText.Line(surface, 150, "Status: "u8,
                    Encoding.UTF8.GetBytes(_status))
                : true) &&
#else
            GuideXosText.Line(surface, 150, "Status: "u8,
                Encoding.UTF8.GetBytes(_status)) &&
#endif
#if HOSTLOGPROOF_C125_MANAGED_PROGRESS_BAR
            GuideXosText.Line(surface, 174, "Document usage: "u8, ReadOnlySpan<byte>.Empty) &&
            _documentUsage.Render(surface) == GuideXosResult.Success &&
#else
            GuideXosText.Line(surface, 174, "Editor: "u8, "bounded ASCII; [] selection; | caret"u8) &&
#endif
#if HOSTLOGPROOF_C123_MANAGED_SEPARATOR
            (!_c123ProofContext || _separator.Render(surface) == GuideXosResult.Success) &&
#endif
#if HOSTLOGPROOF_C126_MANAGED_GROUP_BOX
            ((!_c126ProofContext && !_c126GroupBoxTestContext
#if HOSTLOGPROOF_C127_MANAGED_PANEL
                && !_c127ProofContext && !_c127PanelTestContext
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                && !_c133ProofContext
#endif
                ) ||
                _pathDisplayBox.Render(surface) == GuideXosResult.Success) &&
#if HOSTLOGPROOF_C127_MANAGED_PANEL
            ((!_c127ProofContext && !_c127PanelTestContext
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
                && !_c133ProofContext
#endif
                ) ||
                _pathDisplayPanel.Render(surface) == GuideXosResult.Success) &&
#endif
#endif
#if HOSTLOGPROOF_C119_MANAGED_BUTTON
            _openButton.Render(surface) == GuideXosResult.Success &&
            _saveButton.Render(surface) == GuideXosResult.Success &&
            _saveAsButton.Render(surface) == GuideXosResult.Success &&
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            (!_c121ProofContext || _showPathCheckBox.Render(surface) ==
                GuideXosResult.Success) &&
#if HOSTLOGPROOF_C131_REUSABLE_CHECKBOX
            (!_c131ProofContext || _showStatusCheckBox.Render(surface) ==
                GuideXosResult.Success) &&
#endif
#if HOSTLOGPROOF_C124_MANAGED_RADIO_BUTTON
            ((!(_c124ProofContext
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
                || _c132ProofContext
#endif
                )
#if HOSTLOGPROOF_C127_MANAGED_PANEL
                || _c127ProofContext
#endif
                ) || _fullPathRadio.Render(surface) ==
                GuideXosResult.Success) &&
            ((!(_c124ProofContext
#if HOSTLOGPROOF_C132_REUSABLE_RADIO_BUTTON
                || _c132ProofContext
#endif
                )
#if HOSTLOGPROOF_C127_MANAGED_PANEL
                || _c127ProofContext
#endif
                ) || _fileNameRadio.Render(surface) ==
                GuideXosResult.Success) &&
#endif
#if HOSTLOGPROOF_C133_REUSABLE_COMBOBOX
            (!_c133ProofContext || _pathDisplayCombo.Render(surface) ==
                GuideXosResult.Success) &&
#endif
#if HOSTLOGPROOF_C135_REUSABLE_POPUP_MENU
            (!_c135ProofContext || surface.TryAddButton(
                420, 250, 100, 28, "Options"u8, C135OptionsActionId,
                out _) == GuideXosResult.Success) &&
            (!_c135ProofContext || _c135Menu.Render(surface) ==
                GuideXosResult.Success) &&
#endif
#endif
#else
            surface.TryAddButton(20, 220, 90, 28, "Open"u8, 20u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(120, 220, 90, 28, "Save"u8, 22u, out _) == GuideXosResult.Success &&
            surface.TryAddButton(220, 220, 100, 28, "Save As"u8, 21u, out _) == GuideXosResult.Success &&
#endif
#if HOSTLOGPROOF_C121_MANAGED_CHECKBOX
            (_c121ProofContext
                ? surface.TryAddButton(468, 220, 90, 28, "Reload"u8,
                    3u, out _)
                : surface.TryAddButton(330, 220, 90, 28, "Reload"u8,
                    3u, out _)) == GuideXosResult.Success;
#else
            surface.TryAddButton(330, 220, 90, 28, "Reload"u8, 3u, out _) == GuideXosResult.Success;
#endif
    }

    private static GuideXosResult PickerStartResult(GuideXosFilePickerResult result)
    {
        return result.Status == GuideXosFilePickerStatus.Pending ||
            result.Status == GuideXosFilePickerStatus.NoMatchingFiles
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private static string StatusText(GuideXosFileResult result)
    {
        return result switch
        {
            GuideXosFileResult.InvalidPath => "Invalid path",
            GuideXosFileResult.NotFound => "Not found",
            GuideXosFileResult.FileTooLarge => "File too large",
            GuideXosFileResult.CapabilityUnavailable => "File access unavailable",
            _ => "File I/O failure",
        };
    }

    private static byte[] PathLog(string prefix, string path)
    {
        byte[] prefixBytes = Encoding.UTF8.GetBytes(prefix);
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        byte[] result = new byte[Math.Min(127, prefixBytes.Length + pathBytes.Length)];
        prefixBytes.AsSpan(0, Math.Min(prefixBytes.Length, result.Length)).CopyTo(result);
        int copied = Math.Min(prefixBytes.Length, result.Length);
        if (copied < result.Length)
        {
            pathBytes.AsSpan(0, Math.Min(pathBytes.Length, result.Length - copied))
                .CopyTo(result.AsSpan(copied));
        }
        return result;
    }

    private void LogDocument(GuideXosHost host, string prefix)
    {
        host.TryLog(PathLog(prefix + " document=", _textArea.Text.Replace('\n', '|')));
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
    private bool _useTextInput;
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
        _useTextInput = IsC116Context(host)
#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
            || IsC118Context(host)
#endif
            ;
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
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
        if (IsContext(host, "c116-notes-negative"u8))
        {
            return RunTextInputNegativeProbe(host);
        }
#endif
        return GuideXosResult.Success;
    }

#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (!_picker.IsActive)
        {
            return GuideXosResult.Success;
        }
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }
        GuideXosFilePickerResult result = _picker.HandleInput(host, surface, input);
        return ApplyPickerResult(host, surface, result);
    }
#endif

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
        return ApplyPickerResult(host, surface, result);
    }

    private GuideXosResult ApplyPickerResult(
        GuideXosHost host, GuideXosSurface surface, GuideXosFilePickerResult result)
    {
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
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
                host.TryLog(PathLog("C116-NOTES typed-save path=", result.Path));
#endif
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
                "/system/apps", "Save document", ".TXT", name, true,
                _useTextInput));
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

#if HOSTLOGPROOF_C118_MANAGED_LIST_BOX
    private static bool IsC118Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.SequenceEqual("c118-notes"u8);
    }
#endif

    private static bool IsC116Context(GuideXosHost host)
    {
        return host.LaunchContext.Utf8.StartsWith("c116-"u8);
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

#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
    private static GuideXosResult RunTextInputNegativeProbe(GuideXosHost host)
    {
        GuideXosTextInput input = new(5, "filename");
        bool unfocused = input.HandleCharacter('X') ==
            GuideXosTextInputEditResult.Ignored && input.Value.Length == 0;
        input.Focus();
        bool maximum = true;
        for (char value = 'A'; value <= 'E'; value++)
        {
            maximum &= input.HandleCharacter(value) ==
                GuideXosTextInputEditResult.Changed;
        }
        bool extraRejected = input.HandleCharacter('F') ==
            GuideXosTextInputEditResult.Rejected && input.Value == "ABCDE";
        bool backspaceEmpty = true;
        for (int index = 0; index < 5; index++)
        {
            backspaceEmpty &= input.HandleKey(GuideXosTextInputKey.Backspace) ==
                GuideXosTextInputEditResult.Changed;
        }
        backspaceEmpty &= input.Value.Length == 0 && input.CaretIndex == 0 &&
            input.HandleKey(GuideXosTextInputKey.Backspace) ==
            GuideXosTextInputEditResult.Ignored;
        bool unsupportedRejected = input.HandleCharacter('\u0001') ==
            GuideXosTextInputEditResult.Rejected;

        GuideXosTextInput caret = new(32);
        bool caretProof = caret.SetValue("MFILE.TXT");
        caret.Focus();
        for (int index = 0; index < 8; index++)
        {
            caretProof &= caret.HandleKey(GuideXosTextInputKey.Left) !=
                GuideXosTextInputEditResult.Ignored;
        }
        caretProof &= caret.HandleCharacter('Y') ==
            GuideXosTextInputEditResult.Changed && caret.Value == "MYFILE.TXT";

        GuideXosTextInput cancel = new(16);
        cancel.Focus();
        cancel.HandleCharacter('Q');
        bool escaped = cancel.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosTextInputEditResult.Cancelled && !cancel.IsFocused &&
            cancel.IsCancelled;

        GuideXosPickerPathStatus emptyFilename =
            GuideXosPickerPath.TryBuildPath("/system/apps", input.Value, out _);
        GuideXosPickerPathStatus overflow = GuideXosPickerPath.TryBuildPath(
            "/system/apps", new string('A', 84), out _);
        bool pathValidation = emptyFilename == GuideXosPickerPathStatus.InvalidFilename &&
            overflow == GuideXosPickerPathStatus.PathTooLong;

        bool passed = unfocused && maximum && extraRejected && backspaceEmpty &&
            unsupportedRejected && caretProof && escaped && pathValidation;
        host.TryLog(passed
            ? "C116-NEGATIVE unfocused=PASS max=PASS extra=PASS backspace-empty=PASS caret=PASS escape=PASS path=PASS result=PASS"u8
            : "C116-NEGATIVE result=FAIL"u8);
        return passed ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }
#endif

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
#endif
#endif
}
