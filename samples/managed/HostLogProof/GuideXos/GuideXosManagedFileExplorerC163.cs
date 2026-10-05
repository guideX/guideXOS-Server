#if HOSTLOGPROOF_C163_MANAGED_FILE_EXPLORER
using System;
using System.Globalization;
using System.Text;

namespace HostLogProof;

internal enum GuideXosFileExplorerCommandC163
{
    None = 0,
    Refresh = 1,
    Close = 2,
}

internal abstract class GuideXosFileExplorerSourceC163
{
    public abstract GuideXosFileResult Load(string path,
        out GuideXosDirectoryEntry[] entries, out bool hasMore);
    public abstract GuideXosFileResult Stat(string path,
        out GuideXosFileInfo info);
}

/// <summary>
/// Production source delegates to the exact bounded directory and path code
/// used by the C151/C152 managed dialogs. No VFS handle escapes these calls.
/// </summary>
internal sealed class GuideXosFileExplorerHostSourceC163 :
    GuideXosFileExplorerSourceC163
{
    private GuideXosHost _host;

    internal void Attach(GuideXosHost host) => _host = host;
    internal void Detach() => _host = null;

    public override GuideXosFileResult Load(string path,
        out GuideXosDirectoryEntry[] entries, out bool hasMore)
    {
        if (_host == null)
        {
            entries = Array.Empty<GuideXosDirectoryEntry>();
            hasMore = false;
            return GuideXosFileResult.InvalidArgument;
        }
        return GuideXosDirectoryListing.Load(_host, path, out entries,
            out hasMore);
    }

    public override GuideXosFileResult Stat(string path,
        out GuideXosFileInfo info)
    {
        info = null;
        if (_host == null) return GuideXosFileResult.InvalidArgument;
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryNormalizeDirectory(
                ParentPath(path), out string directory);
        if (pathStatus != GuideXosPickerPathStatus.Success ||
            GuideXosPickerPath.TryBuildPath(directory, LeafName(path),
                out string canonical) != GuideXosPickerPathStatus.Success ||
            !string.Equals(canonical, path, StringComparison.Ordinal))
        {
            return GuideXosFileResult.InvalidPath;
        }
        return GuideXosFile.TryGetInfo(_host, Encoding.UTF8.GetBytes(canonical),
            out info);
    }

    private static int LastSeparator(string path) =>
        path == null ? -1 : path.LastIndexOf('/');
    private static string ParentPath(string path)
    {
        int separator = LastSeparator(path);
        return separator <= 0 ? string.Empty : path.Substring(0, separator);
    }
    private static string LeafName(string path)
    {
        int separator = LastSeparator(path);
        return separator < 0 || separator + 1 >= path.Length
            ? string.Empty : path.Substring(separator + 1);
    }
}

/// <summary>
/// One authoritative, bounded current-directory snapshot. Selection matching
/// is name plus VFS type within this directory, not filesystem identity.
/// </summary>
internal sealed class GuideXosDirectoryBrowserC163
{
    public const int EntryCapacity = GuideXosOpenFileDialog.MaximumDirectoryEntries;
    public const int PathCapacity = GuideXosOpenFileDialog.MaximumPathBytes;
    public const int NameCapacity = GuideXosOpenFileDialog.MaximumNameBytes;
    public const string InitialPath = GuideXosOpenFileDialog.ManagedVfsRoot;

    private readonly GuideXosFileExplorerSourceC163 _source;
    private GuideXosDirectoryEntry[] _entries = Array.Empty<GuideXosDirectoryEntry>();
    private int _entryCount;
    private bool _hasMore;
    private string _currentPath = string.Empty;
    private string _status = "Directory not loaded";
    private int _selectedIndex = -1;
    private ulong? _selectedFileSize;

    internal GuideXosDirectoryBrowserC163(
        GuideXosFileExplorerSourceC163 source)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
    }

    public string CurrentPath => _currentPath;
    public string Status => _status;
    public int EntryCount => _entryCount;
    public bool HasMore => _hasMore;
    public int SelectedIndex => _selectedIndex;
    public bool HasSelection => _selectedIndex >= 0 && _selectedIndex < _entryCount;
    public GuideXosDirectoryEntry SelectedEntry =>
        HasSelection ? _entries[_selectedIndex] : null;
    public ulong? SelectedFileSize => _selectedFileSize;
    public bool CanGoUp => GuideXosDirectoryListing.TryGetParent(
        _currentPath, InitialPath, out _);

    public GuideXosDirectoryEntry EntryAt(int index) =>
        index >= 0 && index < _entryCount ? _entries[index] : null;

    public GuideXosFileResult OpenInitial()
    {
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryNormalizeDirectory(InitialPath,
                out string normalized);
        if (pathStatus != GuideXosPickerPathStatus.Success ||
            !string.Equals(normalized, InitialPath, StringComparison.Ordinal))
        {
            SetStatus("Initial directory path is invalid");
            return GuideXosFileResult.InvalidPath;
        }
        return TryLoadAndCommit(normalized, resetSelection: true,
            resetViewport: true);
    }

    public GuideXosFileResult Refresh()
    {
        if (string.IsNullOrEmpty(_currentPath))
        {
            SetStatus("Directory not loaded");
            return GuideXosFileResult.InvalidPath;
        }

        string selectedName = SelectedEntry?.Name;
        GuideXosEntryType? selectedType = SelectedEntry?.Type;
        return TryLoadAndCommit(_currentPath, resetSelection: false,
            resetViewport: false, selectedName, selectedType);
    }

    public GuideXosFileResult NavigateUp()
    {
        if (!CanGoUp)
        {
            SetStatus("Already at the managed VFS root");
            return GuideXosFileResult.Success;
        }
        if (!GuideXosDirectoryListing.TryGetParent(_currentPath, InitialPath,
                out string parent))
        {
            SetStatus("Parent directory is unavailable");
            return GuideXosFileResult.InvalidPath;
        }
        return TryLoadAndCommit(parent, resetSelection: true,
            resetViewport: true);
    }

    public GuideXosFileResult OpenSelected()
    {
        GuideXosDirectoryEntry entry = SelectedEntry;
        if (entry == null)
        {
            SetStatus("Select a directory or file");
            return GuideXosFileResult.InvalidArgument;
        }
        if (entry.Type != GuideXosEntryType.Directory)
        {
            SetStatus("File activation requires the App Model host");
            return GuideXosFileResult.InvalidArgument;
        }
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryBuildPath(_currentPath, entry.Name,
                out string child);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            SetStatus(PathStatusText(pathStatus));
            return GuideXosFileResult.InvalidPath;
        }
        return TryLoadAndCommit(child, resetSelection: true,
            resetViewport: true);
    }

    public bool Select(int index)
    {
        if (index < 0 || index >= _entryCount)
        {
            ClearSelection();
            SetStatus("No entry selected");
            return false;
        }
        _selectedIndex = index;
        _selectedFileSize = null;
        GuideXosDirectoryEntry entry = _entries[index];
        if (entry.Type == GuideXosEntryType.Directory)
        {
            SetStatus("Directory selected");
            return true;
        }

        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryBuildPath(_currentPath, entry.Name,
                out string filePath);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            SetStatus(PathStatusText(pathStatus));
            return true;
        }
        GuideXosFileResult result = _source.Stat(filePath,
            out GuideXosFileInfo info);
        if (result != GuideXosFileResult.Success || info == null ||
            info.Type != GuideXosEntryType.Regular)
        {
            SetStatus(result == GuideXosFileResult.Success
                ? "File metadata type changed" : "File metadata unavailable");
            return true;
        }
        _selectedFileSize = info.Size;
        SetStatus(_hasMore
            ? TruncationStatus() : "File selected");
        return true;
    }

    public void SetViewport(int firstVisibleIndex)
    {
        // The ListBox owns actual viewport clamping. This method remains a
        // no-op on the model and exists only to keep model tests independent.
        _ = firstVisibleIndex;
    }

    public void SetStatus(string status)
    {
        status ??= string.Empty;
        _status = status.Length <= 56 ? status : status.Substring(0, 56);
    }

    public void ClearSelection()
    {
        _selectedIndex = -1;
        _selectedFileSize = null;
    }

    public void Reset()
    {
        _entries = Array.Empty<GuideXosDirectoryEntry>();
        _entryCount = 0;
        _hasMore = false;
        _currentPath = string.Empty;
        _selectedIndex = -1;
        _selectedFileSize = null;
        SetStatus("Directory not loaded");
    }

    private GuideXosFileResult TryLoadAndCommit(string path,
        bool resetSelection, bool resetViewport,
        string preserveName = null, GuideXosEntryType? preserveType = null)
    {
        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryNormalizeDirectory(path,
                out string normalized);
        if (pathStatus != GuideXosPickerPathStatus.Success ||
            !string.Equals(path, normalized, StringComparison.Ordinal))
        {
            SetStatus(PathStatusText(pathStatus));
            return GuideXosFileResult.InvalidPath;
        }

        GuideXosFileResult result = _source.Load(normalized,
            out GuideXosDirectoryEntry[] nextEntries, out bool nextHasMore);
        if (result != GuideXosFileResult.Success || nextEntries == null ||
            nextEntries.Length > EntryCapacity)
        {
            SetStatus(FileResultText(result == GuideXosFileResult.Success
                ? GuideXosFileResult.IoFailure : result));
            return result == GuideXosFileResult.Success
                ? GuideXosFileResult.IoFailure : result;
        }
        for (int index = 0; index < nextEntries.Length; index++)
        {
            GuideXosDirectoryEntry entry = nextEntries[index];
            if (entry == null || string.IsNullOrEmpty(entry.Name) ||
                entry.Name.Length > NameCapacity ||
                (entry.Type != GuideXosEntryType.Directory &&
                    entry.Type != GuideXosEntryType.Regular) ||
                entry.Name == "." || entry.Name == "..")
            {
                SetStatus("Directory listing contained invalid metadata");
                return GuideXosFileResult.IoFailure;
            }
        }

        GuideXosOpenFileDialog.SortForExplorer(nextEntries,
            nextEntries.Length);
        int nextSelection = -1;
        if (!resetSelection && preserveName != null && preserveType.HasValue)
        {
            for (int index = 0; index < nextEntries.Length; index++)
            {
                if (string.Equals(nextEntries[index].Name, preserveName,
                        StringComparison.Ordinal) &&
                    nextEntries[index].Type == preserveType.Value)
                {
                    nextSelection = index;
                    break;
                }
            }
        }

        // Commit all authoritative directory state together only after the
        // complete new snapshot has passed validation.
        _currentPath = normalized;
        _entries = nextEntries;
        _entryCount = nextEntries.Length;
        _hasMore = nextHasMore;
        _selectedIndex = nextSelection;
        _selectedFileSize = null;
        SetStatus(_hasMore ? TruncationStatus() : _entryCount == 0
            ? "Directory is empty" : "Select an entry");

        if (_selectedIndex >= 0 &&
            _entries[_selectedIndex].Type == GuideXosEntryType.Regular)
        {
            Select(_selectedIndex);
        }
        _ = resetViewport;
        return GuideXosFileResult.Success;
    }

    private string TruncationStatus() =>
        "Showing first " + _entryCount.ToString(CultureInfo.InvariantCulture) +
        " entries; more omitted";

    internal static string PathStatusText(GuideXosPickerPathStatus status) =>
        status == GuideXosPickerPathStatus.PathTooLong
            ? "Path exceeds 96 bytes" : "Invalid directory or entry path";

    private static string FileResultText(GuideXosFileResult result) =>
        result switch
        {
            GuideXosFileResult.NotFound => "Directory no longer exists",
            GuideXosFileResult.NotDirectory => "Path is not a directory",
            GuideXosFileResult.CapabilityUnavailable => "VFS listing unavailable",
            _ => "Directory access failed",
        };
}

internal sealed class GuideXosFileExplorerControllerC163
{
    public const int ControlCapacity = 5;
    public const int ListControlId = 1;
    public const int UpControlId = 2;
    public const int OpenControlId = 3;
    public const int RefreshControlId = 4;
    public const int CloseControlId = 5;
    public const int ListX = 20;
    public const int ListY = 70;
    public const int ButtonY = 326;
    public const int ListVisibleRows = 12;
    public const int ListWidthCharacters = 61;

    private readonly GuideXosFileExplorerSourceC163 _source;
    private readonly GuideXosDirectoryBrowserC163 _browser;
    private readonly GuideXosListBox _list = new(
        GuideXosDirectoryBrowserC163.EntryCapacity,
        GuideXosDirectoryBrowserC163.NameCapacity,
        ListVisibleRows, ListWidthCharacters, ListX, ListY);
    private readonly GuideXosButton _up = new(20, ButtonY, 92, 28, "Up");
    private readonly GuideXosButton _open = new(124, ButtonY, 92, 28, "Open");
    private readonly GuideXosButton _refresh = new(228, ButtonY, 112, 28, "Refresh");
    private readonly GuideXosButton _close = new(352, ButtonY, 92, 28, "Close");
    private readonly GuideXosControlHost _controls = new(ControlCapacity);
    private readonly GuideXosLabel _path = new(20, 42, 760, "Path:", 56);
    private readonly GuideXosLabel _name = new(520, 88, 260, "Name:", 56);
    private readonly GuideXosLabel _type = new(520, 112, 260, "Type:", 56);
    private readonly GuideXosLabel _size = new(520, 136, 260, "Size:", 56);
    private readonly GuideXosLabel _status = new(20, 294, 760, "Directory not loaded", 56);
    private bool _controlsInitialized;
    private bool _suppressControlRCharacter;
    private GuideXosHost _host;

    internal GuideXosFileExplorerControllerC163() :
        this(new GuideXosFileExplorerHostSourceC163())
    {
    }

    internal GuideXosFileExplorerControllerC163(
        GuideXosFileExplorerSourceC163 source)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _browser = new GuideXosDirectoryBrowserC163(_source);
    }

    public GuideXosDirectoryBrowserC163 Browser => _browser;
    public GuideXosListBox List => _list;
    public GuideXosControlHost Controls => _controls;
    public GuideXosButton UpButton => _up;
    public GuideXosButton OpenButton => _open;
    public GuideXosButton RefreshButton => _refresh;
    public GuideXosButton CloseButton => _close;
    public int ControlCount => _controls.RegistrationCount;
    public int ControlMaximum => _controls.MaximumControlCount;
    public int SharedControlMaximum => GuideXosControlHost.MaximumSupportedControlCount;
    public int RowCount => _list.ItemCount;
    public int FirstVisibleIndex => _list.FirstVisibleIndex;

    internal void AttachHost(GuideXosHost host)
    {
        _host = host;
        if (_source is GuideXosFileExplorerHostSourceC163 hostSource)
            hostSource.Attach(host);
    }

    public bool InitializeControls()
    {
        if (_controlsInitialized) return false;
        bool registered =
            _controls.TryRegisterListBox(ListControlId, _list) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(UpControlId, _up) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(OpenControlId, _open) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(RefreshControlId, _refresh) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(CloseControlId, _close) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryFocus(ListControlId) == GuideXosControlHostResult.Focused;
        _controlsInitialized = registered;
        return registered;
    }

    public GuideXosFileResult OpenInitial()
    {
        GuideXosFileResult result = _browser.OpenInitial();
        if (result == GuideXosFileResult.Success) RebuildList(resetViewport: true);
        SyncControlsAndDetails();
        return result;
    }

    public GuideXosFileResult Refresh()
    {
        int oldViewport = _list.FirstVisibleIndex;
        string selectedName = _browser.SelectedEntry?.Name;
        GuideXosEntryType? selectedType = _browser.SelectedEntry?.Type;
        GuideXosFileResult result = _browser.Refresh();
        if (result == GuideXosFileResult.Success)
        {
            RebuildList(resetViewport: false, oldViewport,
                selectedName, selectedType);
        }
        SyncControlsAndDetails();
        return result;
    }

    public GuideXosFileResult NavigateUp()
    {
        GuideXosFileResult result = _browser.NavigateUp();
        if (result == GuideXosFileResult.Success &&
            !string.Equals(_browser.Status, "Already at the managed VFS root",
                StringComparison.Ordinal))
        {
            RebuildList(resetViewport: true);
        }
        SyncControlsAndDetails();
        return result;
    }

    public GuideXosFileResult OpenSelected()
    {
        GuideXosDirectoryEntry entry = _browser.SelectedEntry;
        if (entry == null)
        {
            _browser.SetStatus("Select a directory or file");
            SyncControlsAndDetails();
            return GuideXosFileResult.InvalidArgument;
        }
        if (entry.Type == GuideXosEntryType.Directory)
        {
            string previousPath = _browser.CurrentPath;
            GuideXosFileResult navigation = _browser.OpenSelected();
            if (navigation == GuideXosFileResult.Success &&
                !string.Equals(previousPath, _browser.CurrentPath,
                    StringComparison.Ordinal))
                RebuildList(resetViewport: true);
            SyncControlsAndDetails();
            return navigation;
        }

        GuideXosPickerPathStatus pathStatus =
            GuideXosPickerPath.TryBuildPath(_browser.CurrentPath, entry.Name,
                out string selectedPath);
        if (pathStatus != GuideXosPickerPathStatus.Success)
        {
            _browser.SetStatus(GuideXosDirectoryBrowserC163.PathStatusText(pathStatus));
            SyncControlsAndDetails();
            return GuideXosFileResult.InvalidPath;
        }
        if (_host == null)
        {
            _browser.SetStatus("File activation unavailable");
            SyncControlsAndDetails();
            return GuideXosFileResult.CapabilityUnavailable;
        }

        GuideXosFileActivationResult activation = _host.TryRequestFileActivation(
            Encoding.UTF8.GetBytes(selectedPath));
#if HOSTLOGPROOF_C164_FILE_ACTIVATION_PROOF
        if (activation == GuideXosFileActivationResult.Unsupported)
        {
            Span<byte> line = stackalloc byte[224];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C164-FILE-ACTIVATION path="u8);
            GuideXosText.Append(line, ref position,
                Encoding.UTF8.GetBytes(selectedPath));
            GuideXosText.Append(line, ref position,
                " result=unsupported source-retained=true selection=preserved"u8);
            _host.TryLog(line[..position]);
        }
#endif
        _browser.SetStatus(activation switch
        {
            GuideXosFileActivationResult.Accepted => "Opening file in Managed Notes",
            GuideXosFileActivationResult.Unsupported =>
                "No application is associated with this file type.",
            GuideXosFileActivationResult.InvalidPath => "Selected path is invalid",
            GuideXosFileActivationResult.PathTooLong =>
                "Activation path exceeds 96 bytes",
            GuideXosFileActivationResult.Directory =>
                "Selected entry changed to a directory",
            GuideXosFileActivationResult.NotRegularFile =>
                "Selected entry is not a regular file",
            GuideXosFileActivationResult.NotFound => "Selected file is unavailable",
            GuideXosFileActivationResult.IoFailure => "File association lookup failed",
            _ => "File activation unavailable",
        });
        SyncControlsAndDetails();
        return activation == GuideXosFileActivationResult.Accepted
            ? GuideXosFileResult.Success : GuideXosFileResult.InvalidArgument;
    }

    public void Reset()
    {
        _controls.Reset();
        _list.Reset();
        _up.Reset();
        _open.Reset();
        _refresh.Reset();
        _close.Reset();
        _up.SetEnabled(false);
        _open.SetEnabled(false);
        _controlsInitialized = false;
        _suppressControlRCharacter = false;
        _host = null;
        _browser.Reset();
        if (_source is GuideXosFileExplorerHostSourceC163 hostSource)
            hostSource.Detach();
        _path.SetText("Path:");
        _name.SetText("Name:");
        _type.SetText("Type:");
        _size.SetText("Size:");
        _status.SetText("Directory not loaded");
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null ||
            surface.TryFillRect(8, 8, 784, 374, 0x001D2733u) !=
                GuideXosResult.Success ||
            surface.TrySetText(20, 16, "Managed File Explorer | Read only"u8) !=
                GuideXosResult.Success ||
            _path.Render(surface) != GuideXosResult.Success ||
            _list.Render(surface, ListX, ListY) != GuideXosResult.Success ||
            surface.TrySetText(520, 64, "Selected entry"u8) !=
                GuideXosResult.Success ||
            _name.Render(surface) != GuideXosResult.Success ||
            _type.Render(surface) != GuideXosResult.Success ||
            _size.Render(surface) != GuideXosResult.Success ||
            _status.Render(surface) != GuideXosResult.Success ||
            _up.Render(surface) != GuideXosResult.Success ||
            _open.Render(surface) != GuideXosResult.Success ||
            _refresh.Render(surface) != GuideXosResult.Success ||
            _close.Render(surface) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

    public GuideXosFileExplorerCommandC163 RouteInput(GuideXosInputEvent input)
    {
        if (_suppressControlRCharacter && input.Kind == GuideXosInputKind.KeyChar &&
            (input.Character == 'r' || input.Character == 'R'))
        {
            _suppressControlRCharacter = false;
            return GuideXosFileExplorerCommandC163.None;
        }
        _suppressControlRCharacter = false;
        if (input.Kind == GuideXosInputKind.KeyDown && input.Control &&
            (input.KeyCode == (uint)'r' || input.KeyCode == (uint)'R') &&
            !_controls.IsModalActive && !_controls.HasTransientInputCapture &&
            !_controls.HasPointerDragCapture)
        {
            _suppressControlRCharacter = true;
            Refresh();
            return GuideXosFileExplorerCommandC163.Refresh;
        }

        if (input.Kind == GuideXosInputKind.PointerDown &&
            input.Button == GuideXosPointerButton.Primary)
        {
            int target = 0;
            GuideXosControlHostResult routed = GuideXosControlHostResult.Ignored;
            if (Inside(input.X, input.Y, ListX, ListY, _list.Width, _list.Height))
            {
                target = ListControlId;
                routed = _controls.FocusAndRoutePointer(target, input.X, input.Y,
                    ListX, ListY, GuideXosLabel.CharacterWidth, _list.LineHeight);
                SyncSelectionFromList();
            }
            else if (Inside(input.X, input.Y, _up.X, _up.Y, _up.Width, _up.Height))
            {
                target = UpControlId;
                routed = _controls.FocusAndRoutePointer(target, input.X, input.Y);
            }
            else if (Inside(input.X, input.Y, _open.X, _open.Y,
                    _open.Width, _open.Height))
            {
                target = OpenControlId;
                routed = _controls.FocusAndRoutePointer(target, input.X, input.Y);
            }
            else if (Inside(input.X, input.Y, _refresh.X, _refresh.Y,
                    _refresh.Width, _refresh.Height))
            {
                target = RefreshControlId;
                routed = _controls.FocusAndRoutePointer(target, input.X, input.Y);
            }
            else if (Inside(input.X, input.Y, _close.X, _close.Y,
                    _close.Width, _close.Height))
            {
                target = CloseControlId;
                routed = _controls.FocusAndRoutePointer(target, input.X, input.Y);
            }
            return ActivatedCommand(target, routed);
        }

        if (input.Kind == GuideXosInputKind.Wheel &&
            Inside(input.X, input.Y, ListX, ListY, _list.Width, _list.Height))
        {
            _controls.HandleWheel(ListControlId, input.X, input.Y,
                input.WheelDelta, ListX, ListY,
                GuideXosLabel.CharacterWidth, _list.LineHeight);
            return GuideXosFileExplorerCommandC163.None;
        }

        if (input.Kind == GuideXosInputKind.KeyDown ||
            input.Kind == GuideXosInputKind.KeyChar)
        {
            GuideXosControlHostResult result = _controls.HandleInput(input);
            SyncSelectionFromList();
            return ActivatedCommand(_controls.ActiveControlId, result);
        }
        return GuideXosFileExplorerCommandC163.None;
    }

    private GuideXosFileExplorerCommandC163 ActivatedCommand(int target,
        GuideXosControlHostResult result)
    {
        if (target == ListControlId && result == GuideXosControlHostResult.Activated)
        {
            OpenSelected();
            return GuideXosFileExplorerCommandC163.None;
        }
        if (result != GuideXosControlHostResult.Activated)
            return GuideXosFileExplorerCommandC163.None;
        if (target == UpControlId) NavigateUp();
        else if (target == OpenControlId) OpenSelected();
        else if (target == RefreshControlId) Refresh();
        else if (target == CloseControlId) return GuideXosFileExplorerCommandC163.Close;
        return target == RefreshControlId
            ? GuideXosFileExplorerCommandC163.Refresh
            : GuideXosFileExplorerCommandC163.None;
    }

    private void SyncSelectionFromList()
    {
        if (_list.HasSelection && _browser.SelectedIndex != _list.SelectedIndex)
        {
            _browser.Select(_list.SelectedIndex);
            SyncControlsAndDetails();
        }
    }

    private void SyncControlsAndDetails()
    {
        _up.SetEnabled(_browser.CanGoUp);
        _open.SetEnabled(_browser.HasSelection);
        string path = _browser.CurrentPath ?? string.Empty;
        if (path.Length > 50) path = "..." + path.Substring(path.Length - 47);
        _path.SetText("Path: " + path);
        _status.SetText(_browser.Status ?? string.Empty);
        GuideXosDirectoryEntry selected = _browser.SelectedEntry;
        if (selected == null)
        {
            _name.SetText("Name:");
            _type.SetText("Type:");
            _size.SetText("Size:");
            return;
        }
        _name.SetText("Name: " + DisplayName(selected.Name));
        _type.SetText(selected.Type == GuideXosEntryType.Directory
            ? "Type: Directory" : "Type: File");
        _size.SetText(selected.Type != GuideXosEntryType.Regular
            ? "Size: (directory)" : _browser.SelectedFileSize.HasValue
                ? "Size: " + _browser.SelectedFileSize.Value.ToString(
                    CultureInfo.InvariantCulture) + " bytes"
                : "Size: unavailable");
    }

    private void RebuildList(bool resetViewport, int oldViewport = 0,
        string preserveName = null, GuideXosEntryType? preserveType = null)
    {
        _list.Reset();
        for (int index = 0; index < _browser.EntryCount; index++)
        {
            GuideXosDirectoryEntry entry = _browser.EntryAt(index);
            if (!TryAddRow(entry))
            {
                _browser.SetStatus("Directory exceeds visible row capacity");
                break;
            }
        }

        if (_browser.SelectedIndex >= 0 &&
            _browser.SelectedIndex < _list.ItemCount)
        {
            _list.SelectIndex(_browser.SelectedIndex);
            bool selectionFitsPriorViewport = !resetViewport &&
                _browser.SelectedIndex >= oldViewport &&
                _browser.SelectedIndex < oldViewport + _list.VisibleRowCount;
            if (selectionFitsPriorViewport) _list.SetFirstVisibleIndex(oldViewport);
        }
        else
        {
            _list.ClearSelection();
            if (!resetViewport) _list.SetFirstVisibleIndex(oldViewport);
        }
        if (_controlsInitialized)
            _controls.TryFocus(ListControlId);
        _ = preserveName;
        _ = preserveType;
        SyncControlsAndDetails();
    }

    private bool TryAddRow(GuideXosDirectoryEntry entry)
    {
        ReadOnlySpan<char> prefix = entry.Type == GuideXosEntryType.Directory
            ? "[DIR] ".AsSpan() : "[FILE] ".AsSpan();
        Span<char> bounded = stackalloc char[
            GuideXosListBox.MaximumSupportedLabelLength];
        prefix.CopyTo(bounded);
        int length = prefix.Length;
        int nameLength = Math.Min(entry.Name.Length, bounded.Length - length);
        for (int index = 0; index < nameLength; index++)
        {
            char value = entry.Name[index];
            bounded[length + index] = value is >= (char)0x20 and <= (char)0x7E
                ? value : '?';
        }
        length += nameLength;
        return _list.TryAdd(bounded[..length]) ==
            GuideXosListBoxPopulationResult.Added;
    }

    private static string DisplayName(string name)
    {
        if (name.Length <= 50) return name;
        return name.Substring(0, 47) + "...";
    }

    private static bool Inside(int x, int y, int left, int top,
        int width, int height) => x >= left && x < left + width &&
            y >= top && y < top + height;
}
#endif
