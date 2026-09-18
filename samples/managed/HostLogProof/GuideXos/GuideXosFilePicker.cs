using System;
using System.Text;

namespace HostLogProof;

public enum GuideXosFilePickerMode
{
    Open = 1,
    Save = 2,
}

public enum GuideXosFilePickerStatus
{
    Pending = 0,
    Selected = 1,
    Cancelled = 2,
    InvalidRequest = 3,
    CapabilityUnavailable = 4,
    NoMatchingFiles = 5,
    InvalidSelectedPath = 6,
    DirectorySelectionRejected = 7,
    OverwriteConfirmationRequired = 8,
    OverwriteDeclined = 9,
    MetadataFailure = 10,
    IoFailure = 11,
}

/// <summary>
/// Small, platform-neutral Open/Save intent. Open candidates are exposed
/// through the reusable bounded list box; Save keeps its C116 filename field.
/// </summary>
public sealed class GuideXosFilePickerOptions
{
    public GuideXosFilePickerOptions(
        GuideXosFilePickerMode mode,
        string initialDirectory,
        string title,
        string extensionFilter,
        string suggestedFileName,
        bool requireExistingFile,
        bool confirmOverwrite,
        bool useTextInput = false,
        bool useControlHost = false)
    {
        Mode = mode;
        InitialDirectory = initialDirectory;
        Title = title;
        ExtensionFilter = extensionFilter;
        SuggestedFileName = suggestedFileName;
        RequireExistingFile = requireExistingFile;
        ConfirmOverwrite = confirmOverwrite;
        UseTextInput = useTextInput;
        UseControlHost = useControlHost;
    }

    public GuideXosFilePickerMode Mode { get; }
    public string InitialDirectory { get; }
    public string Title { get; }
    public string ExtensionFilter { get; }
    public string SuggestedFileName { get; }
    public bool RequireExistingFile { get; }
    public bool ConfirmOverwrite { get; }
    public bool UseTextInput { get; }
    public bool UseControlHost { get; }

    public static GuideXosFilePickerOptions Open(
        string initialDirectory,
        string title,
        string extensionFilter,
        bool useControlHost = false)
    {
        return new GuideXosFilePickerOptions(
            GuideXosFilePickerMode.Open, initialDirectory, title,
            extensionFilter, string.Empty, true, true, false, useControlHost);
    }

    public static GuideXosFilePickerOptions Save(
        string initialDirectory,
        string title,
        string extensionFilter,
        string suggestedFileName,
        bool confirmOverwrite,
        bool useTextInput = false,
        bool useControlHost = false)
    {
        return new GuideXosFilePickerOptions(
            GuideXosFilePickerMode.Save, initialDirectory, title,
            extensionFilter, suggestedFileName, false, confirmOverwrite,
            useTextInput, useControlHost);
    }
}

public sealed class GuideXosFilePickerCandidate
{
    internal GuideXosFilePickerCandidate(
        string name, GuideXosEntryType type, ulong size)
    {
        Name = name;
        Type = type;
        Size = size;
    }

    public string Name { get; }
    public GuideXosEntryType Type { get; }
    public ulong Size { get; }
}

public sealed class GuideXosFilePickerResult
{
    internal GuideXosFilePickerResult(
        GuideXosFilePickerStatus status,
        string path,
        GuideXosFileInfo info,
        GuideXosFileResult fileResult,
        string message)
    {
        Status = status;
        Path = path;
        Info = info;
        FileResult = fileResult;
        Message = message;
    }

    public GuideXosFilePickerStatus Status { get; }
    public string Path { get; }
    public GuideXosFileInfo Info { get; }
    public GuideXosFileResult FileResult { get; }
    public string Message { get; }

    internal static GuideXosFilePickerResult Pending(string message)
    {
        return new GuideXosFilePickerResult(
            GuideXosFilePickerStatus.Pending, null, null,
            GuideXosFileResult.Success, message);
    }

    internal static GuideXosFilePickerResult Failure(
        GuideXosFilePickerStatus status,
        GuideXosFileResult fileResult,
        string message)
    {
        return new GuideXosFilePickerResult(status, null, null, fileResult, message);
    }

    internal static GuideXosFilePickerResult Selected(
        string path, GuideXosFileInfo info, string message)
    {
        return new GuideXosFilePickerResult(
            GuideXosFilePickerStatus.Selected, path, info,
            GuideXosFileResult.Success, message);
    }
}

public enum GuideXosPickerPathStatus
{
    Success = 0,
    InvalidDirectory = 1,
    InvalidFilename = 2,
    PathTooLong = 3,
    InvalidExtension = 4,
}

/// <summary>
/// Shared bounded path policy for application-level pickers. It mirrors the
/// C113/C114 printable absolute /system/apps VFS contract without exposing
/// VFS implementation details to applications.
/// </summary>
public static class GuideXosPickerPath
{
    public static GuideXosPickerPathStatus TryNormalizeDirectory(
        string directory, out string normalized)
    {
        normalized = null;
        if (string.IsNullOrEmpty(directory))
        {
            return GuideXosPickerPathStatus.InvalidDirectory;
        }
        if (!IsPrintablePath(directory) || directory.IndexOf('\\') >= 0 ||
            directory.IndexOf("//", StringComparison.Ordinal) >= 0 ||
            ContainsTraversal(directory))
        {
            return GuideXosPickerPathStatus.InvalidDirectory;
        }
        if (!directory.StartsWith("/system/apps", StringComparison.Ordinal) ||
            (directory.Length > 12 && directory[12] != '/'))
        {
            return GuideXosPickerPathStatus.InvalidDirectory;
        }
        string candidate = directory;
        while (candidate.Length > "/system/apps".Length &&
               candidate.EndsWith("/", StringComparison.Ordinal))
        {
            candidate = candidate.Substring(0, candidate.Length - 1);
        }
        if (Encoding.UTF8.GetByteCount(candidate) > GxAbi.FilePathMaxBytes)
        {
            return GuideXosPickerPathStatus.PathTooLong;
        }
        normalized = candidate;
        return GuideXosPickerPathStatus.Success;
    }

    public static GuideXosPickerPathStatus TryBuildPath(
        string directory, string fileName, out string path)
    {
        path = null;
        GuideXosPickerPathStatus directoryStatus =
            TryNormalizeDirectory(directory, out string normalizedDirectory);
        if (directoryStatus != GuideXosPickerPathStatus.Success)
        {
            return directoryStatus;
        }
        if (string.IsNullOrEmpty(fileName) || fileName == "." ||
            fileName == ".." || fileName.IndexOf("..", StringComparison.Ordinal) >= 0 ||
            fileName.IndexOf('/') >= 0 || fileName.IndexOf('\\') >= 0 ||
            !IsPrintableFilename(fileName))
        {
            return GuideXosPickerPathStatus.InvalidFilename;
        }
        string combined = normalizedDirectory + "/" + fileName;
        if (Encoding.UTF8.GetByteCount(combined) > GxAbi.FilePathMaxBytes)
        {
            return GuideXosPickerPathStatus.PathTooLong;
        }
        path = combined;
        return GuideXosPickerPathStatus.Success;
    }

    public static bool IsExtensionFilterValid(string extension)
    {
        if (string.IsNullOrEmpty(extension)) return true;
        if (extension.Length < 2 || extension.Length > 9 || extension[0] != '.')
        {
            return false;
        }
        for (int index = 1; index < extension.Length; index++)
        {
            char value = extension[index];
            if (!IsAsciiLetter(value) && !IsAsciiDigit(value)) return false;
        }
        return true;
    }

    public static bool MatchesExtension(string name, string extension)
    {
        return string.IsNullOrEmpty(extension) ||
            (name.Length > extension.Length &&
             name.EndsWith(extension, StringComparison.OrdinalIgnoreCase));
    }

    public static GuideXosPickerPathStatus TryApplySaveExtension(
        string fileName, string extension, out string selectedName)
    {
        selectedName = null;
        if (!IsExtensionFilterValid(extension))
        {
            return GuideXosPickerPathStatus.InvalidExtension;
        }
        if (string.IsNullOrEmpty(fileName))
        {
            return GuideXosPickerPathStatus.InvalidFilename;
        }
        if (string.IsNullOrEmpty(extension))
        {
            selectedName = fileName;
            return GuideXosPickerPathStatus.Success;
        }
        int lastDot = fileName.LastIndexOf('.');
        if (lastDot >= 0 &&
            !fileName.EndsWith(extension, StringComparison.OrdinalIgnoreCase))
        {
            return GuideXosPickerPathStatus.InvalidExtension;
        }
        selectedName = lastDot < 0 ? fileName + extension : fileName;
        return GuideXosPickerPathStatus.Success;
    }

    private static bool IsPrintablePath(string value)
    {
        for (int index = 0; index < value.Length; index++)
        {
            if (value[index] < 0x20 || value[index] > 0x7E) return false;
        }
        return true;
    }

    private static bool IsPrintableFilename(string value)
    {
        return IsPrintablePath(value);
    }

    private static bool ContainsTraversal(string value)
    {
        return value == ".." || value.StartsWith("../", StringComparison.Ordinal) ||
            value.EndsWith("/..", StringComparison.Ordinal) ||
            value.IndexOf("/../", StringComparison.Ordinal) >= 0;
    }

    private static bool IsAsciiLetter(char value)
    {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    }

    private static bool IsAsciiDigit(char value)
    {
        return value >= '0' && value <= '9';
    }
}

/// <summary>
/// Reusable application-owned picker state machine. OpenFile and SaveFile
/// enumerate immediately and return Pending; compositor actions complete the
/// operation with a selected path or an explicit status. The picker never
/// reads or writes document contents.
/// </summary>
public sealed class GuideXosFilePicker
{
    public const uint OpenAction = 100u;
    public const uint NextAction = 101u;
    public const uint SaveAction = 102u;
    public const uint CancelAction = 103u;
    public const uint ConfirmOverwriteAction = 104u;
    public const uint DeclineOverwriteAction = 105u;
    public const int FilenameFieldX = 16;
    public const int FilenameFieldY = 78;
    public const int FilenameFieldWidth = 470;
    public const int FilenameFieldHeight = 28;
    public const int CandidateListX = 20;
    public const int CandidateListOpenY = 94;
    public const int CandidateListSaveY = 116;
    public const int CandidateListLineHeight = 18;

    private GuideXosFilePickerOptions _options;
    private GuideXosDirectorySnapshot _listing;
    private GuideXosDirectorySnapshot _cachedListing;
    private string _cachedDirectory;
    private readonly GuideXosFilePickerCandidate[] _candidates =
        new GuideXosFilePickerCandidate[GuideXosListBox.MaximumSupportedItemCount];
    private int _candidateCount;
    private string _directory;
    private string _proposedFileName;
    private GuideXosTextInput _filenameInput;
    private readonly GuideXosListBox _candidateList =
        new(GuideXosListBox.MaximumSupportedItemCount,
            GuideXosListBox.MaximumSupportedLabelLength, 4, 56);
    private GuideXosControlHost _focusHost;
    private bool _overwritePending;
    private bool _active;
    private GuideXosFilePickerResult _result =
        GuideXosFilePickerResult.Failure(
            GuideXosFilePickerStatus.Cancelled, GuideXosFileResult.Success,
            "Picker not started");

    public bool IsActive => _active;
    public GuideXosFilePickerMode Mode => _options?.Mode ?? 0;
    public string InitialDirectory => _directory;
    public string ProposedFileName => _filenameInput?.Value ?? _proposedFileName;
    public int SelectedIndex => _candidateList.SelectedIndex;
    public string SelectedCandidateLabel => _candidateList.SelectedLabel;
    public GuideXosListBox CandidateList => _candidateList;
    public bool IsOverwritePending => _overwritePending;
    public GuideXosFilePickerCandidate[] Candidates => _candidates;
    public int CandidateCount => _candidateCount;
    public GuideXosFilePickerResult CurrentResult => _result;
    public GuideXosTextInput FilenameInput => _filenameInput;
    public int SaveFilenameMaximumLength => _filenameInput?.MaximumLength ?? 0;
    public GuideXosControlHost FocusHost => _focusHost ??= new GuideXosControlHost(2);

    public static bool IsSelectableOpenType(GuideXosEntryType type)
    {
        return type == GuideXosEntryType.Regular;
    }

    public static bool IsValidSelectionIndex(int index, int count)
    {
        return index >= 0 && index < count;
    }

    public GuideXosFilePickerResult OpenFile(
        GuideXosHost host, GuideXosSurface surface,
        GuideXosFilePickerOptions options)
    {
        return Begin(host, surface, options, GuideXosFilePickerMode.Open);
    }

    public GuideXosFilePickerResult SaveFile(
        GuideXosHost host, GuideXosSurface surface,
        GuideXosFilePickerOptions options)
    {
        return Begin(host, surface, options, GuideXosFilePickerMode.Save);
    }

    /// <summary>
    /// Prepares one bounded directory snapshot before an input callback needs
    /// to open the picker. This keeps the pointer/key dispatch path free of
    /// filesystem enumeration while preserving the picker-owned cache.
    /// </summary>
    public GuideXosFileResult PrimeDirectory(
        GuideXosHost host, string directory)
    {
        if (host == null || string.IsNullOrEmpty(directory))
        {
            return GuideXosFileResult.InvalidArgument;
        }
        GuideXosPickerPathStatus directoryStatus =
            GuideXosPickerPath.TryNormalizeDirectory(
                directory, out string normalizedDirectory);
        if (directoryStatus != GuideXosPickerPathStatus.Success)
        {
            return GuideXosFileResult.InvalidPath;
        }
        if (_cachedListing != null &&
            string.Equals(_cachedDirectory, normalizedDirectory,
                StringComparison.Ordinal))
        {
            return GuideXosFileResult.Success;
        }
        if (!host.HasCapability(GuideXosCapability.DirectoryList) ||
            !host.HasCapability(GuideXosCapability.FileStat))
        {
            return GuideXosFileResult.CapabilityUnavailable;
        }
        GuideXosFileResult result = GuideXosFile.TryListDirectory(
            host, Encoding.UTF8.GetBytes(normalizedDirectory),
            out GuideXosDirectorySnapshot snapshot);
        if (result == GuideXosFileResult.Success && snapshot != null)
        {
            _cachedDirectory = normalizedDirectory;
            _cachedListing = snapshot;
        }
        return result;
    }

    public GuideXosFilePickerResult HandleAction(
        GuideXosHost host, GuideXosSurface surface, uint actionId)
    {
        if (!_active || _options == null)
        {
            _result = GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.InvalidRequest,
                GuideXosFileResult.InvalidArgument, "Picker is not active");
            return _result;
        }
        if (actionId == CancelAction)
        {
            _active = false;
            _overwritePending = false;
            _filenameInput?.ResetTransientState();
            _candidateList.ResetTransientState();
            _focusHost?.Reset();
            _result = GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.Cancelled,
                GuideXosFileResult.Success, "Cancelled");
            return _result;
        }
        if (actionId == NextAction && _options.Mode == GuideXosFilePickerMode.Open)
        {
            if (_candidateCount == 0)
            {
                return SetPickerStatus(
                    GuideXosFilePickerStatus.NoMatchingFiles,
                    GuideXosFileResult.Success, "No matching files", host, surface);
            }
            if (_options.UseControlHost)
            {
                FocusHost.TryFocus(1);
                FocusHost.HandleKey(GuideXosTextInputKey.Down);
            }
            else
            {
                _candidateList.Focus();
                _candidateList.HandleKey(GuideXosTextInputKey.Down);
            }
            _result = GuideXosFilePickerResult.Pending("Selection changed");
            Render(host, surface);
            return _result;
        }
        if (actionId == OpenAction && _options.Mode == GuideXosFilePickerMode.Open)
        {
            return CompleteOpen(host, surface);
        }
        if (actionId == SaveAction && _options.Mode == GuideXosFilePickerMode.Save)
        {
            return BeginOrValidateSave(host, surface);
        }
        if (actionId == ConfirmOverwriteAction &&
            _options.Mode == GuideXosFilePickerMode.Save && _overwritePending)
        {
            return ConfirmOverwrite(host, surface);
        }
        if (actionId == DeclineOverwriteAction &&
            _options.Mode == GuideXosFilePickerMode.Save && _overwritePending)
        {
            _overwritePending = false;
            return SetPickerStatus(
                GuideXosFilePickerStatus.OverwriteDeclined,
                GuideXosFileResult.Success, "Overwrite declined", host, surface);
        }
        return SetPickerStatus(
            GuideXosFilePickerStatus.InvalidSelectedPath,
            GuideXosFileResult.InvalidArgument, "Invalid picker action", host, surface);
    }

    /// <summary>
    /// Routes a compositor pointer event to the Save filename field. The
    /// application supplies local client coordinates; the control owns focus.
    /// </summary>
    public GuideXosResult HandlePointerDown(
        GuideXosHost host, GuideXosSurface surface, int x, int y)
    {
        if (!_active || _options?.Mode != GuideXosFilePickerMode.Save ||
            _filenameInput == null)
        {
            return GuideXosResult.Success;
        }
        if (x < FilenameFieldX || x >= FilenameFieldX + FilenameFieldWidth ||
            y < FilenameFieldY || y >= FilenameFieldY + FilenameFieldHeight)
        {
            return GuideXosResult.Success;
        }
        if (_options.UseControlHost)
        {
            FocusHost.FocusAndRoutePointer(1, x, y);
        }
        else
        {
            _filenameInput.HandlePointerDown(x, y);
        }
        if (host != null) host.TryLog("C116-TEXT-INPUT focus=PASS source=pointer"u8);
        return _filenameInput.Render(surface, 20, 92, "Filename: "u8) ==
            GuideXosResult.Success ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    /// <summary>
    /// Routes one platform-neutral input event to the focused reusable field.
    /// Save remains responsible for validation and completion.
    /// </summary>
    public GuideXosFilePickerResult HandleInput(
        GuideXosHost host, GuideXosSurface surface, GuideXosInputEvent input)
    {
        if (!_active || _options == null)
        {
            return GuideXosFilePickerResult.Pending("Input ignored");
        }

        if (_options.Mode == GuideXosFilePickerMode.Open)
        {
            if (input.Kind == GuideXosInputKind.KeyDown &&
                (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Escape)
            {
                return HandleAction(host, surface, CancelAction);
            }
            if (!_options.UseControlHost)
            {
                GuideXosListBoxResult listResult = input.Kind switch
                {
                    GuideXosInputKind.PointerDown => _candidateList.HandlePointerDown(
                        input.X, input.Y, CandidateListX, CandidateListOpenY,
                        8, CandidateListLineHeight),
                    GuideXosInputKind.KeyDown => _candidateList.HandleKey(
                        (GuideXosTextInputKey)input.KeyCode),
                    _ => GuideXosListBoxResult.Ignored,
                };
                if (listResult == GuideXosListBoxResult.Activated)
                {
                    return CompleteOpen(host, surface);
                }
                if (host != null && listResult == GuideXosListBoxResult.Focused)
                {
                    host.TryLog("C118-LIST focus=PASS"u8);
                }
                else if (host != null &&
                    listResult == GuideXosListBoxResult.SelectionChanged)
                {
                    host.TryLog("C118-LIST selection=changed result=PASS"u8);
                }
                if (listResult == GuideXosListBoxResult.Rejected && host != null)
                {
                    host.TryLog("C118-LIST input=rejected result=PASS"u8);
                }
                _result = GuideXosFilePickerResult.Pending(
                    listResult == GuideXosListBoxResult.Rejected
                        ? "List input rejected" : "Selection changed");
            }
            else
            {
                GuideXosControlHostResult hostResult = input.Kind == GuideXosInputKind.PointerDown
                    ? FocusHost.FocusAndRoutePointer(
                        1, input.X, input.Y, CandidateListX, CandidateListOpenY,
                        8, CandidateListLineHeight)
                    : FocusHost.HandleInput(input);
                if (hostResult == GuideXosControlHostResult.Activated)
                {
                    return CompleteOpen(host, surface);
                }
                if (host != null && hostResult == GuideXosControlHostResult.Focused)
                {
                    host.TryLog("C118-LIST focus=PASS"u8);
                }
                else if (host != null &&
                    hostResult == GuideXosControlHostResult.Changed)
                {
                    host.TryLog("C118-LIST selection=changed result=PASS"u8);
                }
                if (hostResult == GuideXosControlHostResult.Rejected && host != null)
                {
                    host.TryLog("C118-LIST input=rejected result=PASS"u8);
                }
                _result = GuideXosFilePickerResult.Pending(
                    hostResult == GuideXosControlHostResult.Rejected
                        ? "List input rejected" : "Selection changed");
            }
            if (!Render(host, surface))
            {
                return GuideXosFilePickerResult.Failure(
                    GuideXosFilePickerStatus.IoFailure,
                    GuideXosFileResult.IoFailure, "List redraw failed");
            }
            return _result;
        }

        if (_filenameInput == null)
        {
            return GuideXosFilePickerResult.Pending("Input ignored");
        }
        if (!_options.UseControlHost)
        {
            GuideXosTextInputEditResult editResult = GuideXosTextInputEditResult.Ignored;
            if (input.Kind == GuideXosInputKind.PointerDown)
            {
                return HandlePointerDown(host, surface, input.X, input.Y) == GuideXosResult.Success
                    ? GuideXosFilePickerResult.Pending("Input focus changed")
                    : GuideXosFilePickerResult.Failure(
                        GuideXosFilePickerStatus.IoFailure,
                        GuideXosFileResult.IoFailure, "Input redraw failed");
            }
            if (input.Kind == GuideXosInputKind.KeyChar)
            {
                editResult = _filenameInput.HandleCharacter(input.Character);
            }
            else if (input.Kind == GuideXosInputKind.KeyDown)
            {
                editResult = _filenameInput.HandleKey((GuideXosTextInputKey)input.KeyCode);
            }
            if (_filenameInput.IsCancelled)
            {
                _active = false;
                _overwritePending = false;
                _result = GuideXosFilePickerResult.Failure(
                    GuideXosFilePickerStatus.Cancelled,
                    GuideXosFileResult.Success, "Cancelled from filename input");
                return _result;
            }
            _proposedFileName = _filenameInput.Value;
            if (_filenameInput.IsSubmitted)
            {
                return BeginOrValidateSave(host, surface);
            }
            if (editResult == GuideXosTextInputEditResult.Changed ||
                editResult == GuideXosTextInputEditResult.Rejected)
            {
                if (host != null)
                {
                    host.TryLog(editResult == GuideXosTextInputEditResult.Changed
                        ? "C116-TEXT-INPUT edit=changed result=PASS"u8
                        : "C116-TEXT-INPUT edit=rejected result=PASS"u8);
                }
                _result = GuideXosFilePickerResult.Pending(
                    editResult == GuideXosTextInputEditResult.Rejected
                        ? "Input rejected" : "Filename changed");
                if (_filenameInput.Render(surface, 20, 92, "Filename: "u8) !=
                    GuideXosResult.Success)
                {
                    return GuideXosFilePickerResult.Failure(
                        GuideXosFilePickerStatus.IoFailure,
                        GuideXosFileResult.IoFailure, "Input redraw failed");
                }
            }
            return _result;
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            bool filenameHit = input.X >= FilenameFieldX &&
                input.X < FilenameFieldX + FilenameFieldWidth &&
                input.Y >= FilenameFieldY &&
                input.Y < FilenameFieldY + FilenameFieldHeight;
            bool listHit = input.X >= CandidateListX &&
                input.X < CandidateListX + _candidateList.RenderWidth * 8 &&
                input.Y >= CandidateListSaveY &&
                input.Y < CandidateListSaveY +
                    _candidateList.VisibleRowCount * CandidateListLineHeight;
            if (!filenameHit && !listHit)
            {
                return GuideXosFilePickerResult.Pending("Input ignored");
            }
            GuideXosControlHostResult pointerResult = filenameHit
                ? FocusHost.FocusAndRoutePointer(1, input.X, input.Y)
                : FocusHost.FocusAndRoutePointer(
                    2, input.X, input.Y, CandidateListX, CandidateListSaveY,
                    8, CandidateListLineHeight);
            if (pointerResult == GuideXosControlHostResult.Rejected)
            {
                return GuideXosFilePickerResult.Failure(
                    GuideXosFilePickerStatus.IoFailure,
                    GuideXosFileResult.IoFailure, "Input routing failed");
            }
            if (host != null && pointerResult == GuideXosControlHostResult.Focused)
            {
                host.TryLog(filenameHit
                    ? "C116-TEXT-INPUT focus=PASS source=pointer"u8
                    : "C118-LIST focus=PASS"u8);
            }
            _result = GuideXosFilePickerResult.Pending("Input focus changed");
            return Render(host, surface) ? _result :
                GuideXosFilePickerResult.Failure(
                    GuideXosFilePickerStatus.IoFailure,
                    GuideXosFileResult.IoFailure, "Input redraw failed");
        }
        GuideXosControlHostResult saveHostResult = FocusHost.HandleInput(input);
        if (saveHostResult == GuideXosControlHostResult.Cancelled)
        {
            _active = false;
            _overwritePending = false;
            _focusHost?.Reset();
            _result = GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.Cancelled,
                GuideXosFileResult.Success, "Cancelled from filename input");
            return _result;
        }
        if (saveHostResult == GuideXosControlHostResult.Submitted)
        {
            return BeginOrValidateSave(host, surface);
        }
        if (saveHostResult == GuideXosControlHostResult.Changed ||
            saveHostResult == GuideXosControlHostResult.Rejected)
        {
            _proposedFileName = _filenameInput.Value;
            if (host != null)
            {
                host.TryLog(saveHostResult == GuideXosControlHostResult.Changed
                    ? "C116-TEXT-INPUT edit=changed result=PASS"u8
                    : "C116-TEXT-INPUT edit=rejected result=PASS"u8);
            }
            _result = GuideXosFilePickerResult.Pending(
                saveHostResult == GuideXosControlHostResult.Rejected
                    ? "Input rejected" : "Filename changed");
            if (_filenameInput.Render(surface, 20, 92, "Filename: "u8) !=
                GuideXosResult.Success)
            {
                return GuideXosFilePickerResult.Failure(
                    GuideXosFilePickerStatus.IoFailure,
                    GuideXosFileResult.IoFailure, "Input redraw failed");
            }
        }
        return _result;
    }

    public void Reset()
    {
        _options = null;
        _listing = null;
        ClearCandidates();
        _directory = null;
        _proposedFileName = null;
        _filenameInput = null;
        _candidateList.Reset();
        _focusHost?.Reset();
        _overwritePending = false;
        _active = false;
        _result = GuideXosFilePickerResult.Failure(
            GuideXosFilePickerStatus.Cancelled, GuideXosFileResult.Success,
            "Picker reset");
    }

    private GuideXosFilePickerResult Begin(
        GuideXosHost host, GuideXosSurface surface,
        GuideXosFilePickerOptions options, GuideXosFilePickerMode mode)
    {
        Reset();
        _options = options;
        if (host == null || surface == null || options == null || options.Mode != mode ||
            string.IsNullOrEmpty(options.Title) || options.Title.Length > 48 ||
            !GuideXosPickerPath.IsExtensionFilterValid(options.ExtensionFilter))
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidRequest,
                GuideXosFileResult.InvalidArgument, "Invalid picker request", host, surface);
        }
        GuideXosPickerPathStatus directoryStatus =
            GuideXosPickerPath.TryNormalizeDirectory(options.InitialDirectory, out _directory);
        if (directoryStatus != GuideXosPickerPathStatus.Success)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidRequest,
                GuideXosFileResult.InvalidPath, PathStatusText(directoryStatus), host, surface);
        }
        if (!host.HasCapability(GuideXosCapability.DirectoryList) ||
            !host.HasCapability(GuideXosCapability.FileStat))
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.CapabilityUnavailable,
                GuideXosFileResult.CapabilityUnavailable,
                "Picker capability unavailable", host, surface);
        }
        if (mode == GuideXosFilePickerMode.Save)
        {
            if (string.IsNullOrEmpty(options.SuggestedFileName))
            {
                return SetPickerStatus(
                    GuideXosFilePickerStatus.InvalidRequest,
                    GuideXosFileResult.InvalidArgument,
                    "Save filename is required", host, surface);
            }
            GuideXosPickerPathStatus nameStatus =
                GuideXosPickerPath.TryApplySaveExtension(
                    options.SuggestedFileName, options.ExtensionFilter,
                    out _proposedFileName);
            if (nameStatus != GuideXosPickerPathStatus.Success)
            {
                return SetPickerStatus(
                    GuideXosFilePickerStatus.InvalidRequest,
                    GuideXosFileResult.InvalidPath, PathStatusText(nameStatus), host, surface);
            }
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
            if (options.UseTextInput)
            {
                int filenameMaximum = (int)GxAbi.FilePathMaxBytes -
                    Encoding.UTF8.GetByteCount(_directory) - 1;
                if (filenameMaximum < 1 ||
                    filenameMaximum > GuideXosTextInput.MaximumSupportedLength ||
                    _proposedFileName.Length > filenameMaximum)
                {
                    return SetPickerStatus(
                        GuideXosFilePickerStatus.InvalidRequest,
                        GuideXosFileResult.InvalidPath, "Save filename exceeds path bound",
                        host, surface);
                }
                _filenameInput = new GuideXosTextInput(filenameMaximum, "filename");
                if (!_filenameInput.SetValue(_proposedFileName))
                {
                    return SetPickerStatus(
                        GuideXosFilePickerStatus.InvalidRequest,
                        GuideXosFileResult.InvalidPath, "Save filename is not supported",
                        host, surface);
                }
                host.TryLog("C116-PICKER save-input=initialized"u8);
            }
#endif
            host.TryLog("C115-PICKER phase=save-name-ok"u8);
        }
        bool useCachedListing =
            _cachedListing != null &&
            string.CompareOrdinal(_cachedDirectory, _directory) == 0;
        GuideXosFileResult listResult = GuideXosFileResult.Success;
        if (useCachedListing)
        {
            _listing = _cachedListing;
        }
        else
        {
            listResult = GuideXosFile.TryListDirectory(
                host, Encoding.UTF8.GetBytes(_directory), out _listing);
            if (listResult == GuideXosFileResult.Success && _listing != null)
            {
                _cachedDirectory = _directory;
                _cachedListing = _listing;
            }
        }
        if (listResult != GuideXosFileResult.Success || _listing == null)
        {
            GuideXosFilePickerStatus status = listResult == GuideXosFileResult.CapabilityUnavailable
                ? GuideXosFilePickerStatus.CapabilityUnavailable
                : GuideXosFilePickerStatus.InvalidRequest;
            return SetPickerStatus(status, listResult, "Directory enumeration failed", host, surface);
        }
        BuildCandidates();
        if (mode == GuideXosFilePickerMode.Open)
        {
            if (_options.UseControlHost)
            {
                FocusHost.TryRegisterListBox(1, _candidateList);
                FocusHost.TryFocus(1);
            }
            else
            {
                _candidateList.Focus();
            }
        }
        else if (_options.UseControlHost)
        {
            if (_filenameInput != null)
            {
                FocusHost.TryRegisterTextInput(1, _filenameInput);
            }
            FocusHost.TryRegisterListBox(
                _filenameInput == null ? 1 : 2, _candidateList);
            if (_filenameInput != null)
            {
                FocusHost.TryFocus(1);
            }
            else if (_candidateCount > 0)
            {
                FocusHost.TryFocus(1);
            }
        }
        else if (_candidateCount > 0)
        {
            _candidateList.Focus();
        }
        _active = true;
        _result = _candidateCount == 0 && mode == GuideXosFilePickerMode.Open
            ? GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.NoMatchingFiles,
                GuideXosFileResult.Success, "No matching files")
            : GuideXosFilePickerResult.Pending("Picker ready");
        if (!Render(host, surface))
        {
            _result = GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.IoFailure,
                GuideXosFileResult.IoFailure, "Picker surface failed");
        }
        return _result;
    }

    private void BuildCandidates()
    {
        GuideXosDirectoryEntry[] entries = _listing.Entries;
        int count = 0;
        for (int index = 0; index < _candidateCount; index++)
        {
            _candidates[index] = null;
        }
        _candidateList.Clear();
        for (int index = 0; index < entries.Length; index++)
        {
            GuideXosDirectoryEntry entry = entries[index];
            if (entry.Type == GuideXosEntryType.Directory ||
                GuideXosPickerPath.MatchesExtension(entry.Name, _options.ExtensionFilter))
            {
                if (_candidateList.TryAdd(entry.Name) ==
                    GuideXosListBoxPopulationResult.Added)
                {
                    _candidates[count++] = new GuideXosFilePickerCandidate(
                        entry.Name, entry.Type, entry.Size);
                }
            }
        }
        _candidateCount = count;
    }

    private void ClearCandidates()
    {
        for (int index = 0; index < _candidateCount; index++)
        {
            _candidates[index] = null;
        }
        _candidateCount = 0;
    }

    private GuideXosFilePickerResult CompleteOpen(
        GuideXosHost host, GuideXosSurface surface)
    {
        if (!IsValidSelectionIndex(_candidateList.SelectedIndex, _candidateCount))
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidSelectedPath,
                GuideXosFileResult.InvalidArgument, "No file selected", host, surface);
        }
        GuideXosFilePickerCandidate candidate =
            _candidates[_candidateList.SelectedIndex];
        if (candidate == null || !IsSelectableOpenType(candidate.Type))
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.DirectorySelectionRejected,
                GuideXosFileResult.NotDirectory, "A directory is not a file", host, surface);
        }
        GuideXosPickerPathStatus pathStatus = GuideXosPickerPath.TryBuildPath(
            _directory, candidate.Name, out string path);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidSelectedPath,
                GuideXosFileResult.InvalidPath, PathStatusText(pathStatus), host, surface);
        }
        GuideXosFileResult statResult = GuideXosFile.TryGetInfo(
            host, Encoding.UTF8.GetBytes(path), out GuideXosFileInfo info);
        if (statResult != GuideXosFileResult.Success || info == null)
        {
            return SetPickerStatus(
                statResult == GuideXosFileResult.CapabilityUnavailable
                    ? GuideXosFilePickerStatus.CapabilityUnavailable
                    : GuideXosFilePickerStatus.MetadataFailure,
                statResult, "Selected file metadata failed", host, surface);
        }
        if (info.Type != GuideXosEntryType.Regular)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.DirectorySelectionRejected,
                GuideXosFileResult.NotDirectory, "A directory is not a file", host, surface);
        }
        _active = false;
        _result = GuideXosFilePickerResult.Selected(path, info, "File selected");
        return _result;
    }

    private GuideXosFilePickerResult BeginOrValidateSave(
        GuideXosHost host, GuideXosSurface surface)
    {
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
        if (_filenameInput != null) _proposedFileName = _filenameInput.Value;
#endif
        GuideXosPickerPathStatus pathStatus = GuideXosPickerPath.TryBuildPath(
            _directory, _proposedFileName, out string path);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidSelectedPath,
                GuideXosFileResult.InvalidPath, PathStatusText(pathStatus), host, surface);
        }
        GuideXosFilePickerCandidate existing = FindCandidate(_proposedFileName);
        if (existing == null)
        {
            AddCachedFile(_proposedFileName);
            _active = false;
            _result = GuideXosFilePickerResult.Selected(path, null,
                "New save destination selected");
            return _result;
        }
        GuideXosFileResult statResult = GuideXosFile.TryGetInfo(
            host, Encoding.UTF8.GetBytes(path), out GuideXosFileInfo info);
        if (statResult == GuideXosFileResult.CapabilityUnavailable)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.CapabilityUnavailable, statResult,
                "Save metadata unavailable", host, surface);
        }
        if (statResult == GuideXosFileResult.Success && info != null)
        {
            if (info.Type != GuideXosEntryType.Regular)
            {
                return SetPickerStatus(
                    GuideXosFilePickerStatus.DirectorySelectionRejected,
                    GuideXosFileResult.NotDirectory, "Destination is a directory", host, surface);
            }
            if (!_options.ConfirmOverwrite)
            {
                return SetPickerStatus(
                    GuideXosFilePickerStatus.OverwriteConfirmationRequired,
                    GuideXosFileResult.Success, "Overwrite confirmation required", host, surface);
            }
            _overwritePending = true;
            _result = GuideXosFilePickerResult.Failure(
                GuideXosFilePickerStatus.OverwriteConfirmationRequired,
                GuideXosFileResult.Success, "Confirm overwrite");
            Render(host, surface);
            return _result;
        }
        if (statResult != GuideXosFileResult.NotFound)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.MetadataFailure, statResult,
                "Save destination metadata failed", host, surface);
        }
        AddCachedFile(_proposedFileName);
        _active = false;
        _result = GuideXosFilePickerResult.Selected(path, null, "New save destination selected");
        return _result;
    }

    private GuideXosFilePickerResult ConfirmOverwrite(
        GuideXosHost host, GuideXosSurface surface)
    {
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
        if (_filenameInput != null) _proposedFileName = _filenameInput.Value;
#endif
        _overwritePending = false;
        GuideXosPickerPathStatus pathStatus = GuideXosPickerPath.TryBuildPath(
            _directory, _proposedFileName, out string path);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            return SetPickerStatus(
                GuideXosFilePickerStatus.InvalidSelectedPath,
                GuideXosFileResult.InvalidPath, PathStatusText(pathStatus), host, surface);
        }
        GuideXosFileResult statResult = GuideXosFile.TryGetInfo(
            host, Encoding.UTF8.GetBytes(path), out GuideXosFileInfo info);
        if (statResult == GuideXosFileResult.Success && info != null &&
            info.Type == GuideXosEntryType.Regular)
        {
            _active = false;
            _result = GuideXosFilePickerResult.Selected(path, info, "Overwrite confirmed");
            return _result;
        }
        if (statResult == GuideXosFileResult.NotFound)
        {
            _active = false;
            _result = GuideXosFilePickerResult.Selected(path, null, "Destination is new");
            return _result;
        }
        return SetPickerStatus(
            statResult == GuideXosFileResult.CapabilityUnavailable
                ? GuideXosFilePickerStatus.CapabilityUnavailable
                : GuideXosFilePickerStatus.MetadataFailure,
            statResult, "Overwrite metadata failed", host, surface);
    }

    private GuideXosFilePickerCandidate FindCandidate(string name)
    {
        for (int index = 0; index < _candidateCount; index++)
        {
            GuideXosFilePickerCandidate candidate = _candidates[index];
            if (candidate != null &&
                candidate.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                return candidate;
            }
        }
        return null;
    }

    private void AddCachedFile(string name)
    {
        if (_cachedListing == null || FindCandidate(name) != null) return;
        GuideXosDirectoryEntry[] entries = _cachedListing.Entries;
        if (entries.Length >= GxAbi.MaxDirectoryEntries) return;
        GuideXosDirectoryEntry[] updated =
            new GuideXosDirectoryEntry[entries.Length + 1];
        for (int index = 0; index < entries.Length; index++)
        {
            updated[index] = entries[index];
        }
        updated[entries.Length] = new GuideXosDirectoryEntry(
            name, GuideXosEntryType.Regular, 0u);
        for (int outer = 1; outer < updated.Length; outer++)
        {
            GuideXosDirectoryEntry value = updated[outer];
            int inner = outer - 1;
            while (inner >= 0 &&
                string.CompareOrdinal(updated[inner].Name, value.Name) > 0)
            {
                updated[inner + 1] = updated[inner];
                inner--;
            }
            updated[inner + 1] = value;
        }
        _cachedListing = new GuideXosDirectorySnapshot(updated, false);
        _listing = _cachedListing;
    }

    private GuideXosFilePickerResult SetPickerStatus(
        GuideXosFilePickerStatus status, GuideXosFileResult fileResult,
        string message, GuideXosHost host, GuideXosSurface surface)
    {
        _result = GuideXosFilePickerResult.Failure(status, fileResult, message);
        _active = status != GuideXosFilePickerStatus.Cancelled &&
            status != GuideXosFilePickerStatus.Selected;
        if (host != null && surface != null) Render(host, surface);
        return _result;
    }

    private bool Render(GuideXosHost host, GuideXosSurface surface)
    {
        if (host == null || surface == null) return false;
        if (surface.TryFillRect(10, 10, 500, 230, 0x005A6A9Au) != GuideXosResult.Success)
        {
            return false;
        }
        if (!PickerLine(surface, 24, "Managed Notes | "u8,
                _options?.Title ?? "File picker") ||
            !PickerLine(surface, 48, "Directory: "u8,
                _directory ?? "<invalid>") ||
            !PickerLine(surface, 70, "Filter: "u8,
                string.IsNullOrEmpty(_options?.ExtensionFilter)
                    ? "<all files>" : _options.ExtensionFilter)) return false;
        if (_options?.Mode == GuideXosFilePickerMode.Save)
        {
#if HOSTLOGPROOF_C116_MANAGED_TEXT_INPUT
            if (_filenameInput != null)
            {
                if (_filenameInput.Render(surface, 20, 92, "Filename: "u8) !=
                    GuideXosResult.Success) return false;
            }
            else
#endif
            if (!PickerLine(surface, 92, "Proposed: "u8,
                _proposedFileName ?? "<none>")) return false;
        }

        int firstY = _options?.Mode == GuideXosFilePickerMode.Save
            ? CandidateListSaveY : CandidateListOpenY;
        if (_candidateList.Render(
                surface, CandidateListX, firstY, CandidateListLineHeight) !=
            GuideXosResult.Success)
        {
            return false;
        }
        string status = _result?.Message ?? "Ready";
        if (_candidateCount == 0 && _options?.Mode == GuideXosFilePickerMode.Save)
        {
            status = "No existing match; new file is allowed";
        }
        if (_overwritePending) status = "Overwrite existing file?";
        if (!PickerLine(surface, 190, "Status: "u8, status))
        {
            return false;
        }
        if (_options?.Mode == GuideXosFilePickerMode.Open)
        {
            if (surface.TryAddButton(20, 210, 90, 28, "Open"u8, OpenAction, out _) !=
                    GuideXosResult.Success) return false;
            if (surface.TryAddButton(120, 210, 90, 28, "Next"u8, NextAction, out _) !=
                    GuideXosResult.Success) return false;
            return surface.TryAddButton(220, 210, 90, 28, "Cancel"u8, CancelAction, out _) ==
                GuideXosResult.Success;
        }
        if (_overwritePending)
        {
            if (surface.TryAddButton(20, 210, 110, 28, "Overwrite"u8,
                    ConfirmOverwriteAction, out _) != GuideXosResult.Success) return false;
            return surface.TryAddButton(140, 210, 100, 28, "Decline"u8,
                DeclineOverwriteAction, out _) == GuideXosResult.Success;
        }
        if (surface.TryAddButton(20, 210, 90, 28, "Save"u8, SaveAction, out _) !=
                GuideXosResult.Success) return false;
        return surface.TryAddButton(120, 210, 90, 28, "Cancel"u8, CancelAction, out _) ==
            GuideXosResult.Success;
    }

    private static string PathStatusText(GuideXosPickerPathStatus status)
    {
        return status switch
        {
            GuideXosPickerPathStatus.InvalidDirectory => "Invalid directory",
            GuideXosPickerPathStatus.InvalidFilename => "Invalid filename",
            GuideXosPickerPathStatus.PathTooLong => "Path exceeds 96 bytes",
            GuideXosPickerPathStatus.InvalidExtension => "Invalid extension",
            _ => "Invalid path",
        };
    }

    private static bool PickerLine(
        GuideXosSurface surface, int y, ReadOnlySpan<byte> prefix, string value)
    {
        Span<byte> line = stackalloc byte[64];
        int position = 0;
        return GuideXosText.Append(line, ref position, prefix) &&
            AppendUtf8Bounded(line, ref position, value) &&
            surface.TrySetText(20, y, line[..position]) == GuideXosResult.Success;
    }

    private static bool AppendUtf8Bounded(
        Span<byte> buffer, ref int position, string value)
    {
        if (value == null) return true;
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        int length = Math.Min(bytes.Length, buffer.Length - position);
        if (length < 0) return false;
        bytes.AsSpan(0, length).CopyTo(buffer[position..]);
        position += length;
        return true;
    }
}
