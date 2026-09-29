using System;
using System.Text;

namespace HostLogProof;

public enum GuideXosSaveFileDialogStatus
{
    None = 0,
    Pending = 1,
    Selected = 2,
    OverwriteRequired = 3,
    Cancelled = 4,
    Error = 5,
}

public readonly struct GuideXosSaveFileDialogResult
{
    internal GuideXosSaveFileDialogResult(
        GuideXosSaveFileDialogStatus status, string path,
        GuideXosFileResult fileResult, string message)
    {
        Status = status;
        Path = path;
        FileResult = fileResult;
        Message = message ?? string.Empty;
    }

    public GuideXosSaveFileDialogStatus Status { get; }
    public string Path { get; }
    public GuideXosFileResult FileResult { get; }
    public string Message { get; }
}

/// <summary>
/// Reusable bounded Save File chooser. It shares C151's VFS snapshot model,
/// natural directory ordering, path policy, ListBox and modal input routing.
/// It returns a destination only; callers own document writes and overwrite UI.
/// </summary>
public sealed class GuideXosSaveFileDialog
{
    public const int MaximumRowCount = GuideXosOpenFileDialog.MaximumRowCount;
    public const int MaximumPathBytes = GuideXosOpenFileDialog.MaximumPathBytes;
    public const int MaximumNameBytes = GuideXosOpenFileDialog.MaximumNameBytes;
    public const string ManagedVfsRoot = GuideXosOpenFileDialog.ManagedVfsRoot;

    private enum RowKind : byte
    {
        None = 0,
        Parent = 1,
        Directory = 2,
        Regular = 3,
        More = 4,
        Invalid = 5,
    }

    private struct Row
    {
        public GuideXosDirectoryEntry Entry;
        public RowKind Kind;
    }

    private const int ListControlId = 1;
    private const int FilenameControlId = 2;
    private const int SaveControlId = 3;
    private const int CancelControlId = 4;
    private const int MaximumStatusLength = 56;
    private const string ParentLabel = "[UP] ..";
    private const string MoreLabel = "[MORE] Additional entries omitted";

    private readonly GuideXosDialog _dialog;
    private readonly GuideXosLabel _pathLabel;
    private readonly GuideXosListBox _listBox;
    private readonly GuideXosTextInput _filenameInput;
    private readonly GuideXosLabel _statusLabel;
    private readonly GuideXosButton _saveButton;
    private readonly GuideXosButton _cancelButton;
    private readonly bool _configurationValid;
    private readonly Row[] _rows = new Row[MaximumRowCount];
    private GuideXosHost _host;
    private GuideXosControlHost _parentHost;
    private string _currentDirectory;
    private string _selectedPath;
    private int _entryCount;
    private int _rowCount;
    private bool _active;
    private GuideXosSaveFileDialogResult _result;

    public GuideXosSaveFileDialog()
    {
        _dialog = new GuideXosDialog(12, 12, 496, 276, "Save File", 8);
        _pathLabel = new GuideXosLabel(28, 42, 464, string.Empty, 56);
        _listBox = new GuideXosListBox(
            MaximumRowCount, MaximumNameBytes, 7, 30, 28, 70, 18);
        _filenameInput = new GuideXosTextInput(
            MaximumNameBytes, "untitled.txt", "Filename: ");
        bool filenameBoundsValid = _filenameInput.TrySetBounds(300, 70, 180);
        _statusLabel = new GuideXosLabel(28, 218, 464, string.Empty,
            MaximumStatusLength);
        _saveButton = new GuideXosButton(280, 246, 92, 28, "Save");
        _cancelButton = new GuideXosButton(380, 246, 92, 28, "Cancel");

        _configurationValid = filenameBoundsValid &&
            _dialog.TryAddMember(_listBox) &&
            _dialog.TryAddMember(_filenameInput) &&
            _dialog.TryAddMember(_saveButton) &&
            _dialog.TryAddMember(_cancelButton) &&
            _dialog.TryAddMember(_pathLabel, false) &&
            _dialog.TryAddMember(_statusLabel, false) &&
            _dialog.TrySetDefaultButton(_saveButton);
        _dialog.Closed = OnDialogClosed;
        _result = new GuideXosSaveFileDialogResult(
            GuideXosSaveFileDialogStatus.None, null,
            GuideXosFileResult.Success, string.Empty);
    }

    public bool IsOpen => _active && _dialog.IsOpen;
    public int RegistrationCount => _dialog.RegistrationCount;
    public int RegistrationCapacity => _dialog.RegistrationCapacity;
    public int RowCount => _rowCount;
    public int EntryCount => _entryCount;
    public string CurrentDirectory => _currentDirectory ?? string.Empty;
    public string FileName => _filenameInput.Value;
    public string SelectedPath => _result.Status is
        GuideXosSaveFileDialogStatus.Selected or
        GuideXosSaveFileDialogStatus.OverwriteRequired ? _result.Path : null;
    public GuideXosSaveFileDialogResult Result => _result;
    public GuideXosDialog Dialog => _dialog;
    public GuideXosListBox ListBox => _listBox;
    public GuideXosTextInput FilenameInput => _filenameInput;
    public GuideXosButton SaveButton => _saveButton;
    public GuideXosButton CancelButton => _cancelButton;

    public GuideXosSaveFileDialogResult Open(
        GuideXosHost host, GuideXosSurface surface,
        GuideXosControlHost parentHost, string initialDirectory,
        string initialFileName = "untitled.txt", bool restoreChooserState = false)
    {
        if (_active || !_configurationValid || host == null || surface == null ||
            parentHost == null)
        {
            return SetError(GuideXosFileResult.InvalidArgument,
                "Save request is invalid");
        }
        if (!host.HasCapability(GuideXosCapability.FileWrite) ||
            !host.HasCapability(GuideXosCapability.DirectoryList) ||
            !host.HasCapability(GuideXosCapability.FileStat))
        {
            return SetError(GuideXosFileResult.CapabilityUnavailable,
                "Save capability unavailable");
        }

        if (!restoreChooserState || string.IsNullOrEmpty(_currentDirectory))
        {
            GuideXosPickerPathStatus pathStatus =
                GuideXosPickerPath.TryNormalizeDirectory(
                    initialDirectory, out string normalizedDirectory);
            if (pathStatus != GuideXosPickerPathStatus.Success)
            {
                return SetError(GuideXosFileResult.InvalidPath,
                    "Initial directory is invalid");
            }
            _currentDirectory = normalizedDirectory;
            string name = string.IsNullOrEmpty(initialFileName)
                ? "untitled.txt" : initialFileName;
            if (!_filenameInput.SetValue(name))
            {
                return SetError(GuideXosFileResult.InvalidPath,
                    "Initial filename exceeds the VFS component limit");
            }
        }

        _host = host;
        _parentHost = parentHost;
        _selectedPath = null;
        _result = Pending("Loading directory");
        GuideXosFileResult listing = RefreshDirectory();
        if (listing != GuideXosFileResult.Success)
        {
            _parentHost = null;
            _host = null;
            return SetError(listing, FileErrorText(listing));
        }
        if (!_dialog.Open(parentHost))
        {
            _parentHost = null;
            _host = null;
            return SetError(GuideXosFileResult.InvalidArgument,
                "Modal scope is unavailable");
        }
        _active = true;
        _filenameInput.ResetTransientState();
        _dialog.ControlHost.TryFocus(ListControlId);
        _result = Pending(_statusLabel.Text);
        if (_dialog.Render(surface) != GuideXosResult.Success)
        {
            _dialog.Close(GuideXosDialogResult.Cancel);
            return SetError(GuideXosFileResult.IoFailure,
                "Dialog rendering failed");
        }
        return _result;
    }

    public GuideXosSaveFileDialogResult HandleInput(
        GuideXosSurface surface, GuideXosInputEvent input)
    {
        if (!IsOpen || surface == null) return _result;
        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Enter &&
            _dialog.ControlHost.ActiveControlId == ListControlId)
        {
            NavigateSelected(surface);
            return _result;
        }

        int previousSelection = _listBox.SelectedIndex;
        int previousViewport = _listBox.FirstVisibleIndex;
        GuideXosControlHostResult routed = _dialog.HandleInput(input);
        if (input.Kind == GuideXosInputKind.Wheel)
        {
            _host?.TryLog(_listBox.FirstVisibleIndex != previousViewport
                ? "C152-SAVE-DIALOG-NATIVE-WHEEL viewport=moved result=PASS"u8
                : "C152-SAVE-DIALOG-NATIVE-WHEEL viewport=unchanged result=FAIL"u8);
        }
        if (_dialog.IsOpen && previousSelection != _listBox.SelectedIndex)
        {
            PopulateNameFromSelectedFile();
        }
        if (_dialog.IsOpen && routed == GuideXosControlHostResult.Activated)
        {
            int activeId = _dialog.ControlHost.ActiveControlId;
            if (activeId == SaveControlId) TryCompleteSave(surface);
            else if (activeId == CancelControlId)
                _dialog.Close(GuideXosDialogResult.Cancel);
        }
        if (_dialog.IsOpen) _dialog.Render(surface);
        return _result;
    }

    public GuideXosResult Render(GuideXosSurface surface) =>
        _dialog.Render(surface);

    public bool Cancel() => _dialog.Close(GuideXosDialogResult.Cancel);

    private GuideXosFileResult RefreshDirectory()
    {
        ClearRows();
        GuideXosFileResult result = GuideXosDirectoryListing.Load(
            _host, _currentDirectory, out GuideXosDirectoryEntry[] entries,
            out bool snapshotHasMore);
        if (result != GuideXosFileResult.Success || entries == null)
            return result == GuideXosFileResult.Success
                ? GuideXosFileResult.IoFailure : result;

        bool hasParent = !string.Equals(_currentDirectory, ManagedVfsRoot,
            StringComparison.Ordinal);
        int actualLimit = GuideXosOpenFileDialog.ComputeEntryLimit(
            entries.Length, hasParent, snapshotHasMore, MaximumRowCount,
            out bool truncated);
        if (hasParent) AddRow(default, RowKind.Parent, ParentLabel);
        _entryCount = Math.Min(entries.Length, actualLimit);
        for (int index = 0; index < _entryCount; index++)
        {
            GuideXosDirectoryEntry entry = entries[index];
            GuideXosPickerPathStatus pathStatus =
                GuideXosPickerPath.TryBuildPath(
                    _currentDirectory, entry.Name, out _);
            RowKind kind = pathStatus == GuideXosPickerPathStatus.Success
                ? entry.Type == GuideXosEntryType.Directory
                    ? RowKind.Directory : RowKind.Regular
                : RowKind.Invalid;
            if (!AddRow(entry, kind, FormatEntryLabel(entry)))
                return GuideXosFileResult.IoFailure;
        }
        if (truncated && !AddRow(default, RowKind.More, MoreLabel))
            return GuideXosFileResult.IoFailure;

        if (!_pathLabel.SetText(FormatPath(_currentDirectory)))
            return GuideXosFileResult.IoFailure;
        _listBox.Reset();
        for (int index = 0; index < _rowCount; index++)
        {
            Row row = _rows[index];
            string label = row.Kind switch
            {
                RowKind.Parent => ParentLabel,
                RowKind.More => MoreLabel,
                _ => FormatEntryLabel(row.Entry),
            };
            if (_listBox.TryAdd(label) != GuideXosListBoxPopulationResult.Added)
                return GuideXosFileResult.IoFailure;
        }
        if (_dialog.ControlHost.TryFocus(ListControlId) !=
            GuideXosControlHostResult.Focused)
            return GuideXosFileResult.IoFailure;
        SetStatus(truncated
            ? "Showing bounded entries; more items available"
            : _entryCount == 0
                ? "Directory is empty"
                : "Enter a directory or type a filename");
        return GuideXosFileResult.Success;
    }

    private void NavigateSelected(GuideXosSurface surface)
    {
        if (!_listBox.IsValidIndex(_listBox.SelectedIndex)) return;
        Row row = _rows[_listBox.SelectedIndex];
        if (row.Kind == RowKind.Parent)
        {
            if (!GuideXosDirectoryListing.TryGetParent(
                    _currentDirectory, ManagedVfsRoot, out string parent))
            {
                SetStatus("Already at the managed VFS root");
                _dialog.Render(surface);
                return;
            }
            TryChangeDirectory(parent, surface);
            return;
        }
        if (row.Kind == RowKind.Directory && row.Entry != null)
        {
            if (GuideXosPickerPath.TryBuildPath(_currentDirectory,
                    row.Entry.Name, out string path) ==
                GuideXosPickerPathStatus.Success)
            {
                TryChangeDirectory(path, surface);
            }
            return;
        }
        SetStatus(row.Kind == RowKind.More
            ? "Showing a bounded directory snapshot"
            : "Select a directory or enter a filename");
        _dialog.Render(surface);
    }

    private void TryChangeDirectory(string directory, GuideXosSurface surface)
    {
        string previous = _currentDirectory;
        _currentDirectory = directory;
        GuideXosFileResult result = RefreshDirectory();
        if (result != GuideXosFileResult.Success)
        {
            _currentDirectory = previous;
            RefreshDirectory();
            SetStatus(FileErrorText(result));
        }
        _result = Pending(_statusLabel.Text);
        _dialog.Render(surface);
    }

    private void PopulateNameFromSelectedFile()
    {
        if (!_listBox.IsValidIndex(_listBox.SelectedIndex)) return;
        Row row = _rows[_listBox.SelectedIndex];
        if (row.Kind == RowKind.Regular && row.Entry != null)
        {
            _filenameInput.SetValue(row.Entry.Name);
            SetStatus("Existing file selected; Save will ask before replacing it");
        }
        else if (row.Kind == RowKind.Directory)
        {
            SetStatus("Directory selected; press Enter to open it");
        }
        else if (row.Kind == RowKind.Parent)
        {
            SetStatus("Parent selected; press Enter to go up");
        }
    }

    private void TryCompleteSave(GuideXosSurface surface)
    {
        string fileName = _filenameInput.Value;
        bool targetValid = TryBuildTargetPath(_currentDirectory, fileName,
            out string targetPath, out GuideXosPickerPathStatus pathStatus);
        if (!targetValid)
        {
            SetStatus(pathStatus == GuideXosPickerPathStatus.PathTooLong
                ? "Filename and directory exceed 96 bytes"
                : "Filename is invalid for the managed VFS");
            _dialog.Render(surface);
            return;
        }

        GuideXosFileResult parentResult = GuideXosFile.TryGetInfo(
            _host, Encoding.UTF8.GetBytes(_currentDirectory),
            out GuideXosFileInfo parentInfo);
        if (parentResult != GuideXosFileResult.Success || parentInfo == null ||
            parentInfo.Type != GuideXosEntryType.Directory)
        {
            SetStatus(FileErrorText(parentResult));
            _dialog.Render(surface);
            return;
        }

        GuideXosFileResult stat = GuideXosFile.TryGetInfo(
            _host, Encoding.UTF8.GetBytes(targetPath),
            out GuideXosFileInfo targetInfo);
        if (stat == GuideXosFileResult.Success)
        {
            if (targetInfo == null || targetInfo.Type != GuideXosEntryType.Regular)
            {
                SetStatus("A directory already uses this filename");
                _dialog.Render(surface);
                return;
            }
            _selectedPath = targetPath;
            _dialog.Close(GuideXosDialogResult.OK);
            _result = new GuideXosSaveFileDialogResult(
                GuideXosSaveFileDialogStatus.OverwriteRequired, targetPath,
                GuideXosFileResult.Success, "Existing file requires confirmation");
            return;
        }
        if (stat != GuideXosFileResult.NotFound)
        {
            SetStatus(FileErrorText(stat));
            _dialog.Render(surface);
            return;
        }

        _selectedPath = targetPath;
        _dialog.Close(GuideXosDialogResult.OK);
        _result = new GuideXosSaveFileDialogResult(
            GuideXosSaveFileDialogStatus.Selected, targetPath,
            GuideXosFileResult.Success, "New file target selected");
    }

    private bool AddRow(GuideXosDirectoryEntry entry, RowKind kind, string label)
    {
        if (_rowCount >= MaximumRowCount || label == null ||
            label.Length > GuideXosListBox.MaximumSupportedLabelLength)
            return false;
        _rows[_rowCount++] = new Row { Entry = entry, Kind = kind };
        return true;
    }

    private void ClearRows()
    {
        Array.Clear(_rows);
        _rowCount = 0;
        _entryCount = 0;
        _listBox.Reset();
    }

    private void SetStatus(string status)
    {
        if (status == null) status = string.Empty;
        if (status.Length > MaximumStatusLength)
            status = status.Substring(0, MaximumStatusLength);
        _statusLabel.SetText(status);
        if (_active) _result = Pending(status);
    }

    private void OnDialogClosed(GuideXosDialogResult result)
    {
        _active = false;
        _parentHost = null;
        _host = null;
        if (result != GuideXosDialogResult.OK || string.IsNullOrEmpty(_selectedPath))
        {
            _selectedPath = null;
            _result = new GuideXosSaveFileDialogResult(
                GuideXosSaveFileDialogStatus.Cancelled, null,
                GuideXosFileResult.Success, "Save cancelled");
        }
    }

    private GuideXosSaveFileDialogResult SetError(
        GuideXosFileResult fileResult, string message)
    {
        _active = false;
        _selectedPath = null;
        _result = new GuideXosSaveFileDialogResult(
            GuideXosSaveFileDialogStatus.Error, null, fileResult, message);
        return _result;
    }

    internal static bool IsValidVfsFilename(string fileName)
    {
        if (string.IsNullOrEmpty(fileName) || fileName == "." || fileName == ".." ||
            fileName.Length > MaximumNameBytes ||
            Encoding.UTF8.GetByteCount(fileName) > MaximumNameBytes)
        {
            return false;
        }
        for (int index = 0; index < fileName.Length; index++)
        {
            char value = fileName[index];
            if (value < '!' || value > '~' || value == '/' || value == '\\')
                return false;
            if (value == '.' && index + 1 < fileName.Length &&
                fileName[index + 1] == '.') return false;
        }
        return true;
    }

    internal static bool TryBuildTargetPath(string directory, string fileName,
        out string path, out GuideXosPickerPathStatus status)
    {
        path = null;
        status = GuideXosPickerPathStatus.InvalidFilename;
        if (!IsValidVfsFilename(fileName)) return false;
        status = GuideXosPickerPath.TryBuildPath(directory, fileName, out path);
        return status == GuideXosPickerPathStatus.Success;
    }

    private static string FormatEntryLabel(GuideXosDirectoryEntry entry)
    {
        string prefix = entry.Type == GuideXosEntryType.Directory
            ? "[DIR] " : "[FILE] ";
        int count = Math.Min(entry.Name.Length,
            GuideXosListBox.MaximumSupportedLabelLength - prefix.Length);
        return prefix + entry.Name.Substring(0, count);
    }

    private static string FormatPath(string path)
    {
        if (path.Length <= GuideXosLabel.MaximumSupportedTextLength) return path;
        const int suffixLength = 3;
        return path.Substring(0,
            GuideXosLabel.MaximumSupportedTextLength - suffixLength) + "...";
    }

    private static string FileErrorText(GuideXosFileResult result) => result switch
    {
        GuideXosFileResult.NotFound => "Directory or file not found",
        GuideXosFileResult.NotDirectory => "Path is not a directory",
        GuideXosFileResult.InvalidPath => "Path is invalid",
        GuideXosFileResult.CapabilityUnavailable => "File access unavailable",
        GuideXosFileResult.FileTooLarge => "File exceeds the VFS limit",
        _ => "Directory access failed",
    };

    private static GuideXosSaveFileDialogResult Pending(string message) =>
        new(GuideXosSaveFileDialogStatus.Pending, null,
            GuideXosFileResult.Success, message);
}
