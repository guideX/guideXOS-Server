using System;
using System.Text;

namespace HostLogProof;

public enum GuideXosOpenFileDialogStatus
{
    None = 0,
    Pending = 1,
    Selected = 2,
    Cancelled = 3,
    Error = 4,
}

public readonly struct GuideXosOpenFileDialogResult
{
    internal GuideXosOpenFileDialogResult(
        GuideXosOpenFileDialogStatus status,
        string path,
        GuideXosFileResult fileResult,
        string message)
    {
        Status = status;
        Path = path;
        FileResult = fileResult;
        Message = message ?? string.Empty;
    }

    public GuideXosOpenFileDialogStatus Status { get; }
    public string Path { get; }
    public GuideXosFileResult FileResult { get; }
    public string Message { get; }
}

/// <summary>
/// Application-independent Open File chooser composed from the existing
/// bounded VFS snapshot, ListBox, leaf controls, and Dialog modal scope.
/// The caller owns this component and receives a copied, bounded path.
/// </summary>
public sealed class GuideXosOpenFileDialog
{
    public const int MaximumRowCount = 64;
    public const int MaximumDirectoryEntries = 64;
    public const int MaximumPathBytes = (int)GxAbi.FilePathMaxBytes;
    public const int MaximumNameBytes = (int)GxAbi.MaxDirectoryNameBytes;
    public const string ManagedVfsRoot = "/system/apps";

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
    private const int OpenControlId = 2;
    private const int CancelControlId = 3;
    private const int MaximumStatusLength = 56;
    private const string ParentLabel = "[UP] ..";
    private const string MoreLabel = "[MORE] Additional entries omitted";

    private readonly GuideXosDialog _dialog;
    private readonly GuideXosLabel _pathLabel;
    private readonly GuideXosListBox _listBox;
    private readonly GuideXosLabel _statusLabel;
    private readonly GuideXosButton _openButton;
    private readonly GuideXosButton _cancelButton;
    private readonly bool _configurationValid;
    private readonly GuideXosDirectoryEntry[] _sortedEntries =
        new GuideXosDirectoryEntry[MaximumDirectoryEntries];
    private readonly Row[] _rows = new Row[MaximumRowCount];
    private GuideXosControlHost _parentHost;
    private GuideXosHost _host;
    private string _currentDirectory;
    private string _selectedPath;
    private int _entryCount;
    private int _rowCount;
    private bool _active;
    private GuideXosOpenFileDialogResult _result;

    public GuideXosOpenFileDialog()
    {
        _dialog = new GuideXosDialog(12, 12, 496, 276, "Open File", 8);
        _pathLabel = new GuideXosLabel(28, 42, 464, string.Empty, 56);
        _listBox = new GuideXosListBox(
            MaximumRowCount, MaximumNameBytes, 8, 56, 28, 70, 18);
        _statusLabel = new GuideXosLabel(28, 218, 464, string.Empty,
            MaximumStatusLength);
        _openButton = new GuideXosButton(280, 246, 92, 28, "Open");
        _cancelButton = new GuideXosButton(380, 246, 92, 28, "Cancel");

        bool configured =
            _dialog.TryAddMember(_listBox) &&
            _dialog.TryAddMember(_openButton) &&
            _dialog.TryAddMember(_cancelButton) &&
            _dialog.TryAddMember(_pathLabel, false) &&
            _dialog.TryAddMember(_statusLabel, false) &&
            _dialog.TrySetDefaultButton(_openButton);
        _configurationValid = configured;
        _dialog.Closed = OnDialogClosed;
        _openButton.SetEnabled(false);
        _result = new GuideXosOpenFileDialogResult(
            GuideXosOpenFileDialogStatus.None, null,
            GuideXosFileResult.Success, string.Empty);
    }

    public bool IsOpen => _active && _dialog.IsOpen;
    public int RegistrationCount => _dialog.RegistrationCount;
    public int RegistrationCapacity => _dialog.RegistrationCapacity;
    public int RowCount => _rowCount;
    public int EntryCount => _entryCount;
    public bool HasMore => _rowCount != 0 && _rows[_rowCount - 1].Kind == RowKind.More;
    public string CurrentDirectory => _currentDirectory ?? string.Empty;
    public string SelectedPath => _result.Status == GuideXosOpenFileDialogStatus.Selected
        ? _result.Path : null;
    public GuideXosOpenFileDialogResult Result => _result;
    public GuideXosDialog Dialog => _dialog;
    public GuideXosListBox ListBox => _listBox;
    public GuideXosButton OpenButton => _openButton;
    public GuideXosButton CancelButton => _cancelButton;

    public GuideXosOpenFileDialogResult Open(
        GuideXosHost host,
        GuideXosSurface surface,
        GuideXosControlHost parentHost,
        string initialDirectory)
    {
        if (_active || !_configurationValid || host == null || surface == null ||
            parentHost == null)
        {
            return SetError(GuideXosFileResult.InvalidArgument,
                "Open request is invalid");
        }
        if (!host.HasCapability(GuideXosCapability.DirectoryList) ||
            !host.HasCapability(GuideXosCapability.FileStat))
        {
            return SetError(GuideXosFileResult.CapabilityUnavailable,
                "Open capability unavailable");
        }
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryNormalizeDirectory(
                initialDirectory, out string normalizedDirectory);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            return SetError(GuideXosFileResult.InvalidPath,
                PathErrorText(pathStatus));
        }

        _host = host;
        _parentHost = parentHost;
        _currentDirectory = normalizedDirectory;
        _selectedPath = null;
        _result = Pending("Loading directory");
        GuideXosFileResult listing = RefreshDirectory();
        if (listing != GuideXosFileResult.Success)
        {
            _parentHost = null;
            _host = null;
            _currentDirectory = null;
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
        SyncSelectionStatus();
        _result = Pending(_statusLabel.Text);
        if (Render(surface) != GuideXosResult.Success)
        {
            _dialog.Close(GuideXosDialogResult.Cancel);
            return SetError(GuideXosFileResult.IoFailure,
                "Dialog rendering failed");
        }
        return _result;
    }

    public GuideXosOpenFileDialogResult HandleInput(
        GuideXosSurface surface, GuideXosInputEvent input)
    {
        if (!IsOpen || surface == null) return _result;

        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Enter &&
            _dialog.ControlHost.ActiveControlId == ListControlId)
        {
            if (_listBox.IsValidIndex(_listBox.SelectedIndex))
            {
                Row row = _rows[_listBox.SelectedIndex];
                if (row.Kind == RowKind.Parent)
                {
                    NavigateToParent(surface);
                    return _result;
                }
                if (row.Kind == RowKind.Directory)
                {
                    NavigateInto(row.Entry, surface);
                    return _result;
                }
                if (row.Kind == RowKind.More)
                {
                    SetStatus("Showing a bounded directory snapshot");
                    Render(surface);
                    return _result;
                }
                if (row.Kind == RowKind.Invalid)
                {
                    SetStatus("Entry name is not supported");
                    Render(surface);
                    return _result;
                }
            }
        }

        GuideXosControlHostResult routed = _dialog.HandleInput(input);
        SyncSelectionStatus();
        if (_dialog.IsOpen && routed == GuideXosControlHostResult.Activated)
        {
            int activeId = _dialog.ControlHost.ActiveControlId;
            if (activeId == OpenControlId) TryCompleteOpen(surface);
            else if (activeId == CancelControlId)
                _dialog.Close(GuideXosDialogResult.Cancel);
        }
        if (_dialog.IsOpen) Render(surface);
        return _result;
    }

    public GuideXosResult Render(GuideXosSurface surface) =>
        _dialog.Render(surface);

    public bool Cancel()
    {
        return _dialog.Close(GuideXosDialogResult.Cancel);
    }

    private GuideXosFileResult RefreshDirectory()
    {
        ClearRows();
        GuideXosFileResult result = GuideXosFile.TryListDirectory(
            _host, Encoding.UTF8.GetBytes(_currentDirectory),
            out GuideXosDirectorySnapshot snapshot);
        if (result != GuideXosFileResult.Success || snapshot == null ||
            snapshot.Entries == null)
        {
            return result == GuideXosFileResult.Success
                ? GuideXosFileResult.IoFailure : result;
        }

        bool hasParent = !string.Equals(_currentDirectory, ManagedVfsRoot,
            StringComparison.Ordinal);
        if (snapshot.Entries.Length > MaximumDirectoryEntries)
            return GuideXosFileResult.IoFailure;
        int availableEntryCount = 0;
        for (int index = 0; index < snapshot.Entries.Length; index++)
        {
            GuideXosDirectoryEntry entry = snapshot.Entries[index];
            if (!IsValidMetadata(entry)) return GuideXosFileResult.IoFailure;
            if (entry.Name == "." || entry.Name == "..") continue;
            _sortedEntries[availableEntryCount++] = entry;
        }
        int actualLimit = ComputeEntryLimit(availableEntryCount,
            hasParent, snapshot.HasMore, MaximumRowCount, out bool truncated);
        if (hasParent) AddRow(default, RowKind.Parent, ParentLabel);
        SortForExplorer(_sortedEntries, availableEntryCount);
        _entryCount = Math.Min(availableEntryCount, actualLimit);
        for (int index = 0; index < _entryCount; index++)
        {
            GuideXosDirectoryEntry entry = _sortedEntries[index];
            GuideXosPickerPathStatus pathStatus =
                GuideXosPickerPath.TryBuildPath(
                    _currentDirectory, entry.Name, out _);
            RowKind kind = pathStatus == GuideXosPickerPathStatus.Success
                ? entry.Type == GuideXosEntryType.Directory
                    ? RowKind.Directory : RowKind.Regular
                : RowKind.Invalid;
            string label = FormatEntryLabel(entry);
            if (!AddRow(entry, kind, label)) return GuideXosFileResult.IoFailure;
        }
        if (truncated && !AddRow(default, RowKind.More, MoreLabel))
            return GuideXosFileResult.IoFailure;

        string pathDisplay = FormatPath(_currentDirectory);
        if (!_pathLabel.SetText(pathDisplay)) return GuideXosFileResult.IoFailure;
        _listBox.Reset();
        for (int index = 0; index < _rowCount; index++)
        {
            string label = _rows[index].Kind == RowKind.Parent
                ? ParentLabel
                : _rows[index].Kind == RowKind.More
                    ? MoreLabel
                    : FormatEntryLabel(_rows[index].Entry);
            if (_listBox.TryAdd(label) != GuideXosListBoxPopulationResult.Added)
                return GuideXosFileResult.IoFailure;
        }
        if (_dialog.ControlHost.TryFocus(ListControlId) !=
            GuideXosControlHostResult.Focused)
            return GuideXosFileResult.IoFailure;
        _openButton.SetEnabled(false);
        _statusLabel.SetText(truncated
            ? "Showing bounded entries; more items available"
            : _entryCount == 0
                ? "Directory is empty"
                : "Select a file or enter a directory");
        if (_dialog.IsOpen) SyncSelectionStatus();
        return GuideXosFileResult.Success;
    }

    private void NavigateInto(GuideXosDirectoryEntry entry,
        GuideXosSurface surface)
    {
        if (entry == null || entry.Type != GuideXosEntryType.Directory)
        {
            SetStatus("Selected entry is not a directory");
            Render(surface);
            return;
        }
        if (!TryChangeDirectory(entry.Name, surface))
            SetStatus("Directory path could not be opened");
    }

    private void NavigateToParent(GuideXosSurface surface)
    {
        if (string.Equals(_currentDirectory, ManagedVfsRoot,
                StringComparison.Ordinal))
        {
            SetStatus("Already at the managed VFS root");
            Render(surface);
            return;
        }
        int slash = _currentDirectory.LastIndexOf('/');
        string parent = slash <= ManagedVfsRoot.Length
            ? ManagedVfsRoot : _currentDirectory.Substring(0, slash);
        string previous = _currentDirectory;
        _currentDirectory = parent;
        if (RefreshDirectory() != GuideXosFileResult.Success)
        {
            _currentDirectory = previous;
            RefreshDirectory();
            SetStatus("Parent directory could not be opened");
        }
        Render(surface);
    }

    private bool TryChangeDirectory(string name, GuideXosSurface surface)
    {
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryBuildPath(_currentDirectory, name,
                out string path);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            SetStatus(PathErrorText(pathStatus));
            Render(surface);
            return false;
        }
        string previous = _currentDirectory;
        _currentDirectory = path;
        GuideXosFileResult result = RefreshDirectory();
        if (result != GuideXosFileResult.Success)
        {
            _currentDirectory = previous;
            RefreshDirectory();
            SetStatus(FileErrorText(result));
            Render(surface);
            return false;
        }
        _selectedPath = null;
        _result = Pending(_statusLabel.Text);
        Render(surface);
        return true;
    }

    private void TryCompleteOpen(GuideXosSurface surface)
    {
        if (!_listBox.IsValidIndex(_listBox.SelectedIndex))
        {
            SetStatus("Select a file to open");
            Render(surface);
            return;
        }
        Row row = _rows[_listBox.SelectedIndex];
        if (row.Kind != RowKind.Regular || row.Entry == null)
        {
            SetStatus(row.Kind == RowKind.Directory
                ? "Press Enter to enter this directory"
                : row.Kind == RowKind.Parent
                    ? "Press Enter to go to the parent directory"
                    : row.Kind == RowKind.More
                        ? "Showing a bounded directory snapshot"
                        : "Selected entry cannot be opened");
            Render(surface);
            return;
        }
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryBuildPath(_currentDirectory,
                row.Entry.Name, out string path);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            SetStatus(PathErrorText(pathStatus));
            Render(surface);
            return;
        }
        GuideXosFileResult stat = GuideXosFile.TryGetInfo(
            _host, Encoding.UTF8.GetBytes(path), out GuideXosFileInfo info);
        if (stat != GuideXosFileResult.Success || info == null ||
            info.Type != GuideXosEntryType.Regular)
        {
            SetStatus(stat == GuideXosFileResult.Success
                ? "Selected entry is not a regular file"
                : FileErrorText(stat));
            Render(surface);
            return;
        }

        _selectedPath = path;
        _result = new GuideXosOpenFileDialogResult(
            GuideXosOpenFileDialogStatus.Selected, path,
            GuideXosFileResult.Success, "File selected");
        _dialog.Close(GuideXosDialogResult.OK);
    }

    private void SyncSelectionStatus()
    {
        if (!_listBox.IsValidIndex(_listBox.SelectedIndex))
        {
            _openButton.SetEnabled(false);
            SetStatus(_entryCount == 0
                ? "Directory has no files"
                : "Select a file or directory");
            return;
        }
        Row row = _rows[_listBox.SelectedIndex];
        bool canOpen = row.Kind == RowKind.Regular && row.Entry != null;
        _openButton.SetEnabled(canOpen);
        SetStatus(row.Kind switch
        {
            RowKind.Parent => "Parent directory selected; Enter to go up",
            RowKind.Directory => "Directory selected; Enter to enter",
            RowKind.Regular => "File selected; Enter or Open to continue",
            RowKind.More => "More directory entries are available",
            _ => "Entry cannot be opened",
        });
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
        Array.Clear(_sortedEntries);
        _rowCount = 0;
        _entryCount = 0;
        _listBox.Reset();
        _openButton.SetEnabled(false);
        _selectedPath = null;
    }

    private void SetStatus(string status)
    {
        if (status == null) status = string.Empty;
        if (status.Length > MaximumStatusLength)
            status = status.Substring(0, MaximumStatusLength);
        _statusLabel.SetText(status);
        if (_active)
        {
            _result = Pending(status);
        }
    }

    private void OnDialogClosed(GuideXosDialogResult result)
    {
        _active = false;
        _parentHost = null;
        _host = null;
        if (result == GuideXosDialogResult.OK &&
            !string.IsNullOrEmpty(_selectedPath))
        {
            _result = new GuideXosOpenFileDialogResult(
                GuideXosOpenFileDialogStatus.Selected, _selectedPath,
                GuideXosFileResult.Success, "File selected");
        }
        else
        {
            _selectedPath = null;
            _result = new GuideXosOpenFileDialogResult(
                GuideXosOpenFileDialogStatus.Cancelled, null,
                GuideXosFileResult.Success, "Open cancelled");
        }
    }

    private GuideXosOpenFileDialogResult SetError(
        GuideXosFileResult fileResult, string message)
    {
        _active = false;
        _selectedPath = null;
        _result = Error(fileResult, message);
        return _result;
    }

    private static GuideXosOpenFileDialogResult Pending(string message) =>
        new(GuideXosOpenFileDialogStatus.Pending, null,
            GuideXosFileResult.Success, message);

    private static GuideXosOpenFileDialogResult Error(
        GuideXosFileResult fileResult, string message) =>
        new(GuideXosOpenFileDialogStatus.Error, null, fileResult, message);

    private static bool IsValidMetadata(GuideXosDirectoryEntry entry) =>
        entry != null && !string.IsNullOrEmpty(entry.Name) &&
        entry.Name.Length <= MaximumNameBytes &&
        (entry.Type == GuideXosEntryType.Directory ||
            entry.Type == GuideXosEntryType.Regular);

    internal static void SortForExplorer(
        GuideXosDirectoryEntry[] entries, int count)
    {
        for (int outer = 1; outer < count; outer++)
        {
            GuideXosDirectoryEntry value = entries[outer];
            int inner = outer - 1;
            while (inner >= 0 && CompareForExplorer(entries[inner], value) > 0)
            {
                entries[inner + 1] = entries[inner];
                inner--;
            }
            entries[inner + 1] = value;
        }
    }

    internal static int CompareForExplorer(
        GuideXosDirectoryEntry left, GuideXosDirectoryEntry right)
    {
        if (left.Type != right.Type)
            return left.Type == GuideXosEntryType.Directory ? -1 : 1;
        int length = Math.Min(left.Name.Length, right.Name.Length);
        for (int index = 0; index < length; index++)
        {
            char a = AsciiLower(left.Name[index]);
            char b = AsciiLower(right.Name[index]);
            if (a != b) return a < b ? -1 : 1;
        }
        if (left.Name.Length != right.Name.Length)
            return left.Name.Length < right.Name.Length ? -1 : 1;
        return string.CompareOrdinal(left.Name, right.Name);
    }

    private static char AsciiLower(char value) =>
        value is >= 'A' and <= 'Z' ? (char)(value + ('a' - 'A')) : value;

    internal static int ComputeEntryLimit(int availableEntryCount,
        bool hasParent, bool snapshotHasMore, int maximumRows,
        out bool truncated)
    {
        truncated = availableEntryCount < 0 || maximumRows < 1 ||
            maximumRows > MaximumRowCount;
        if (truncated) return 0;
        int parentRows = hasParent ? 1 : 0;
        truncated = snapshotHasMore || availableEntryCount + parentRows > maximumRows;
        if (!truncated) return Math.Min(availableEntryCount, maximumRows - parentRows);
        int capacity = maximumRows - parentRows - 1;
        return Math.Max(0, Math.Min(availableEntryCount, capacity));
    }

    private static string FormatEntryLabel(GuideXosDirectoryEntry entry)
    {
        ReadOnlySpan<char> prefix = entry.Type == GuideXosEntryType.Directory
            ? "[DIR] ".AsSpan() : "[FILE] ".AsSpan();
        Span<char> line = stackalloc char[MaximumNameBytes];
        int position = 0;
        prefix.CopyTo(line);
        position += prefix.Length;
        int copyCount = Math.Min(entry.Name.Length, line.Length - position);
        for (int index = 0; index < copyCount; index++)
        {
            char value = entry.Name[index];
            line[position++] = value is >= (char)0x20 and <= (char)0x7E
                ? value : '?';
        }
        return new string(line[..position]);
    }

    private static string FormatPath(string path)
    {
        if (path.Length <= GuideXosLabel.MaximumSupportedTextLength) return path;
        const int suffixLength = 3;
        return path.Substring(0,
            GuideXosLabel.MaximumSupportedTextLength - suffixLength) + "...";
    }

    private static string PathErrorText(GuideXosPickerPathStatus status) =>
        status switch
        {
            GuideXosPickerPathStatus.InvalidDirectory => "Invalid directory path",
            GuideXosPickerPathStatus.InvalidFilename => "Invalid filename",
            GuideXosPickerPathStatus.PathTooLong => "Path exceeds 96 bytes",
            _ => "Invalid path",
        };

    private static string FileErrorText(GuideXosFileResult result) =>
        result switch
        {
            GuideXosFileResult.NotFound => "Directory or file not found",
            GuideXosFileResult.NotDirectory => "Path is not a directory",
            GuideXosFileResult.InvalidPath => "Path is invalid",
            GuideXosFileResult.CapabilityUnavailable => "File access unavailable",
            _ => "Directory access failed",
        };
}
