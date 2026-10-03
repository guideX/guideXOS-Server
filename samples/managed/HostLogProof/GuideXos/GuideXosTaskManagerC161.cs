#if HOSTLOGPROOF_C161_MANAGED_TASK_MANAGER
using System;

namespace HostLogProof;

internal enum GuideXosTaskManagerCommandC161
{
    None = 0,
    Refresh = 1,
    Close = 2,
}

/// <summary>
/// Read-only presentation over the authoritative C160 value snapshot. The
/// ListBox stores bounded display strings; selected identity remains the
/// C160 source plus 64-bit lifetime token.
/// </summary>
internal sealed unsafe class GuideXosTaskManagerControllerC161
{
    public const int ApplicationCapacity = (int)GxAbi.ApplicationSnapshotCapacity;
    public const int ControlCapacity = 3;
    public const int ListControlId = 1;
    public const int RefreshControlId = 2;
    public const int CloseControlId = 3;
    public const int ListX = 20;
    public const int ListY = 58;
    public const int ListWidthCharacters = 45;
    public const int ListVisibleRows = 10;
    public const int RefreshX = 20;
    public const int CloseX = 156;
    public const int ButtonY = 276;

    private readonly GuideXosListBox _list = new(
        ApplicationCapacity, 48, ListVisibleRows, ListWidthCharacters,
        ListX, ListY);
    private readonly GuideXosButton _refreshButton = new(
        RefreshX, ButtonY, 120, 28, "Refresh");
    private readonly GuideXosButton _closeButton = new(
        CloseX, ButtonY, 120, 28, "Close");
    private readonly GuideXosControlHost _controls = new(ControlCapacity);
    private readonly GuideXosLabel _name = new(400, 60, 400, "", 50);
    private readonly GuideXosLabel _applicationId = new(400, 78, 400, "", 50);
    private readonly GuideXosLabel _applicationIdContinuation =
        new(400, 96, 400, "", 50);
    private readonly GuideXosLabel _applicationIdContinuation2 =
        new(400, 114, 400, "", 50);
    private readonly GuideXosLabel _lifetime = new(400, 132, 400, "", 56);
    private readonly GuideXosLabel _state = new(400, 150, 400, "", 56);
    private readonly GuideXosLabel _active = new(400, 168, 400, "", 56);
    private readonly GuideXosLabel _source = new(400, 186, 400, "", 56);
    private readonly GuideXosLabel _snapshotStatus = new(20, 248, 760, "", 56);
    private readonly GuideXosLabel _emptyDetails =
        new(400, 60, 400, "No application selected", 56);
    private GuideXosApplicationSnapshot _snapshot;
    private GuideXosApplicationInstanceId _selectedIdentity;
    private GuideXosApplicationSnapshotResult _lastResult =
        GuideXosApplicationSnapshotResult.NotSupported;
    private bool _hasSnapshot;
    private bool _hasSelectedIdentity;
    private bool _suppressControlRCharacter;

    public GuideXosTaskManagerControllerC161()
    {
        SetNoSelectionDetails();
        SetStatus("Application snapshot not loaded".AsSpan());
    }

    public GuideXosListBox List => _list;
    public GuideXosButton RefreshButton => _refreshButton;
    public GuideXosButton CloseButton => _closeButton;
    public GuideXosControlHost Controls => _controls;
    public int ControlCount => _controls.RegistrationCount;
    public int ControlMaximum => _controls.MaximumControlCount;
    public int SharedControlMaximum =>
        GuideXosControlHost.MaximumSupportedControlCount;
    public int RowCount => _list.ItemCount;
    public int FirstVisibleIndex => _list.FirstVisibleIndex;
    public int SelectedIndex => _list.SelectedIndex;
    public bool HasSnapshot => _hasSnapshot;
    public bool HasSelection => _hasSelectedIdentity && _list.HasSelection;
    public GuideXosApplicationInstanceId SelectedIdentity =>
        HasSelection ? _selectedIdentity : default;
    public GuideXosApplicationSnapshotResult LastResult => _lastResult;
    public GuideXosApplicationSnapshot Snapshot => _snapshot;
    public string SelectedNameText => _name.Text;
    public string SelectedApplicationIdText =>
        _applicationId.Text + _applicationIdContinuation.Text +
            _applicationIdContinuation2.Text;
    public string SelectedLifetimeText => _lifetime.Text;
    public string SelectedStateText => _state.Text;
    public string SelectedActiveText => _active.Text;
    public string SelectedSourceText => _source.Text;
    public string StatusText => _snapshotStatus.Text;

    public bool InitializeControls()
    {
        _controls.Reset();
        _list.Reset();
        _refreshButton.Reset();
        _closeButton.Reset();
        bool registered =
            _controls.TryRegisterListBox(ListControlId, _list) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(RefreshControlId, _refreshButton) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryRegisterButton(CloseControlId, _closeButton) ==
                GuideXosControlHostResult.Registered &&
            _controls.TryFocus(ListControlId) ==
                GuideXosControlHostResult.Focused;
        if (!registered) _controls.Reset();
        return registered && _controls.RegistrationCount == ControlCapacity;
    }

    /// <summary>Accepts only a complete C160 wrapper result or its truncation.</summary>
    public bool ApplySnapshot(GuideXosApplicationSnapshotResult result,
        in GuideXosApplicationSnapshot snapshot)
    {
        if (result != GuideXosApplicationSnapshotResult.Success &&
            result != GuideXosApplicationSnapshotResult.Truncated)
        {
            _lastResult = result;
            SetStatus(ErrorStatus(result));
            return false;
        }

        if (snapshot.Count > ApplicationCapacity ||
            snapshot.TotalCount > ApplicationCapacity ||
            !GuideXosHost.IsValidApplicationSnapshotCountTuple(
                snapshot.nativeResult, snapshot.TotalCount, snapshot.Count) ||
            (result == GuideXosApplicationSnapshotResult.Truncated) !=
                snapshot.IsTruncated)
        {
            _lastResult = GuideXosApplicationSnapshotResult.InvalidData;
            SetStatus(ErrorStatus(_lastResult));
            return false;
        }

        Span<char> validationRow = stackalloc char[48];
        for (uint index = 0; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record) ||
                !GuideXosHost.IsValidApplicationSnapshotRecord(ref record) ||
                !TryBuildRow(ref record, validationRow, out _))
            {
                _lastResult = GuideXosApplicationSnapshotResult.InvalidData;
                SetStatus(ErrorStatus(_lastResult));
                return false;
            }
        }

        bool hadSnapshot = _hasSnapshot;
        bool hadSelection = TryGetSelectedRecord(out _, out _);
        GuideXosApplicationInstanceId priorIdentity = hadSelection
            ? _selectedIdentity : default;
        int priorFirstVisibleIndex = _list.FirstVisibleIndex;

        _list.Clear();
        _snapshot = snapshot;
        _hasSnapshot = true;
        _lastResult = result;
        Span<char> row = stackalloc char[48];
        for (uint index = 0; index < snapshot.Count; ++index)
        {
            if (!snapshot.TryGetRecord(index,
                    out GuideXosApplicationSnapshotRecord record) ||
                !TryBuildRow(ref record, row, out int rowLength) ||
                _list.TryAdd(row[..rowLength]) !=
                    GuideXosListBoxPopulationResult.Added)
            {
                _list.Clear();
                _snapshot = default;
                _hasSnapshot = false;
                _hasSelectedIdentity = false;
                _lastResult = GuideXosApplicationSnapshotResult.InvalidData;
                SetNoSelectionDetails();
                SetStatus(ErrorStatus(_lastResult));
                return false;
            }
        }

        int preservedIndex = -1;
        if (hadSelection)
        {
            for (uint index = 0; index < snapshot.Count; ++index)
            {
                if (snapshot.TryGetRecord(index,
                        out GuideXosApplicationSnapshotRecord record) &&
                    record.Identity == priorIdentity)
                {
                    preservedIndex = (int)index;
                    break;
                }
            }
        }

        if (preservedIndex >= 0)
        {
            _list.SelectIndex(preservedIndex);
            int firstVisible = Math.Clamp(priorFirstVisibleIndex, 0,
                _list.MaximumFirstVisibleIndex);
            if (preservedIndex < firstVisible)
                firstVisible = preservedIndex;
            else if (preservedIndex >= firstVisible + _list.VisibleRowCount)
                firstVisible = preservedIndex - _list.VisibleRowCount + 1;
            _list.SetFirstVisibleIndex(Math.Clamp(firstVisible, 0,
                _list.MaximumFirstVisibleIndex));
            _selectedIdentity = priorIdentity;
            _hasSelectedIdentity = true;
        }
        else if (!hadSnapshot && snapshot.Count != 0u)
        {
            // One deterministic initial selection. Later disappearance never
            // attaches to a replacement record, even if its slot was reused.
            _list.SelectIndex(0);
            SyncSelectionFromList();
        }
        else
        {
            _list.ClearSelection();
            if (hadSnapshot)
                _list.SetFirstVisibleIndex(Math.Clamp(priorFirstVisibleIndex,
                    0, _list.MaximumFirstVisibleIndex));
            _hasSelectedIdentity = false;
            _selectedIdentity = default;
        }

        SyncDetails();
        SetSnapshotStatus(snapshot);
        return true;
    }

    public bool TryGetSelectedRecord(
        out GuideXosApplicationSnapshotRecord record, out int index)
    {
        record = default;
        index = _list.SelectedIndex;
        return _hasSnapshot && _list.IsValidIndex(index) &&
            _snapshot.TryGetRecord((uint)index, out record) &&
            (!_hasSelectedIdentity || record.Identity == _selectedIdentity);
    }

    public void SyncSelectionFromList()
    {
        if (!_hasSnapshot || !_list.IsValidIndex(_list.SelectedIndex) ||
            !_snapshot.TryGetRecord((uint)_list.SelectedIndex,
                out GuideXosApplicationSnapshotRecord record))
        {
            _hasSelectedIdentity = false;
            _selectedIdentity = default;
            return;
        }
        _selectedIdentity = record.Identity;
        _hasSelectedIdentity = true;
        SyncDetails();
    }

    public GuideXosTaskManagerCommandC161 RouteInput(GuideXosInputEvent input)
    {
        if (_suppressControlRCharacter &&
            input.Kind == GuideXosInputKind.KeyChar &&
            (input.Character == 'r' || input.Character == 'R'))
        {
            _suppressControlRCharacter = false;
            return GuideXosTaskManagerCommandC161.None;
        }
        _suppressControlRCharacter = false;

        if (input.Kind == GuideXosInputKind.KeyDown && input.Control &&
            (input.KeyCode == (uint)'r' || input.KeyCode == (uint)'R') &&
            !_controls.IsModalActive && !_controls.HasTransientInputCapture &&
            !_controls.HasPointerDragCapture)
        {
            _suppressControlRCharacter = true;
            return GuideXosTaskManagerCommandC161.Refresh;
        }

        if (input.Kind == GuideXosInputKind.PointerDown &&
            input.Button == GuideXosPointerButton.Primary)
        {
            GuideXosControlHostResult result = GuideXosControlHostResult.Ignored;
            int target = 0;
            if (Inside(input.X, input.Y, ListX, ListY,
                    _list.Width, _list.Height))
            {
                target = ListControlId;
                result = _controls.FocusAndRoutePointer(target,
                    input.X, input.Y, ListX, ListY,
                    GuideXosLabel.CharacterWidth, _list.LineHeight);
            }
            else if (Inside(input.X, input.Y, RefreshX, ButtonY,
                    _refreshButton.Width, _refreshButton.Height))
            {
                target = RefreshControlId;
                result = _controls.FocusAndRoutePointer(target,
                    input.X, input.Y);
            }
            else if (Inside(input.X, input.Y, CloseX, ButtonY,
                    _closeButton.Width, _closeButton.Height))
            {
                target = CloseControlId;
                result = _controls.FocusAndRoutePointer(target,
                    input.X, input.Y);
            }
            if (target == ListControlId) SyncSelectionFromList();
            return ActivatedCommand(target, result);
        }

        if (input.Kind == GuideXosInputKind.Wheel &&
            Inside(input.X, input.Y, ListX, ListY,
                _list.Width, _list.Height))
        {
            _controls.HandleWheel(ListControlId, input.X, input.Y,
                input.WheelDelta, ListX, ListY,
                GuideXosLabel.CharacterWidth, _list.LineHeight);
            return GuideXosTaskManagerCommandC161.None;
        }

        if (input.Kind == GuideXosInputKind.KeyDown ||
            input.Kind == GuideXosInputKind.KeyChar)
        {
            GuideXosControlHostResult result = _controls.HandleInput(input);
            SyncSelectionFromList();
            return ActivatedCommand(_controls.ActiveControlId, result);
        }
        return GuideXosTaskManagerCommandC161.None;
    }

    public void Reset()
    {
        _controls.Reset();
        _list.Reset();
        _refreshButton.Reset();
        _closeButton.Reset();
        _snapshot = default;
        _selectedIdentity = default;
        _hasSnapshot = false;
        _hasSelectedIdentity = false;
        _suppressControlRCharacter = false;
        _lastResult = GuideXosApplicationSnapshotResult.NotSupported;
        SetNoSelectionDetails();
        SetStatus("Application snapshot not loaded".AsSpan());
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface.TryFillRect(8, 8, 784, 354, 0x001D2733u) !=
                GuideXosResult.Success ||
            surface.TrySetText(20, 16, "Managed Task Manager | Applications"u8) !=
                GuideXosResult.Success ||
            surface.TrySetText(20, 38, "Applications | C160 snapshot order"u8) !=
                GuideXosResult.Success ||
            surface.TrySetText(400, 38, "Selected Application"u8) !=
                GuideXosResult.Success ||
            _list.Render(surface, ListX, ListY) != GuideXosResult.Success ||
            RenderDetails(surface) != GuideXosResult.Success ||
            _snapshotStatus.Render(surface) != GuideXosResult.Success ||
            _refreshButton.Render(surface) != GuideXosResult.Success ||
            _closeButton.Render(surface) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

    public static ReadOnlySpan<char> ErrorStatus(
        GuideXosApplicationSnapshotResult result) => result switch
    {
        GuideXosApplicationSnapshotResult.NotSupported =>
            "Application snapshot requires host ABI v2".AsSpan(),
        GuideXosApplicationSnapshotResult.CapabilityUnavailable =>
            "Application snapshot capability unavailable".AsSpan(),
        GuideXosApplicationSnapshotResult.InvalidData =>
            "Snapshot refresh failed: invalid data".AsSpan(),
        GuideXosApplicationSnapshotResult.InvalidArgument =>
            "Snapshot refresh failed: invalid argument".AsSpan(),
        _ => "Snapshot refresh failed: native failure".AsSpan(),
    };

    private static GuideXosTaskManagerCommandC161 ActivatedCommand(
        int target, GuideXosControlHostResult result)
    {
        if (result != GuideXosControlHostResult.Activated)
            return GuideXosTaskManagerCommandC161.None;
        return target == RefreshControlId
            ? GuideXosTaskManagerCommandC161.Refresh
            : target == CloseControlId
                ? GuideXosTaskManagerCommandC161.Close
                : GuideXosTaskManagerCommandC161.None;
    }

    private GuideXosResult RenderDetails(GuideXosSurface surface)
    {
        if (!HasSelection)
            return _emptyDetails.Render(surface);
        if (_name.Render(surface) != GuideXosResult.Success ||
            _applicationId.Render(surface) != GuideXosResult.Success ||
            _applicationIdContinuation.Render(surface) != GuideXosResult.Success ||
            _applicationIdContinuation2.Render(surface) != GuideXosResult.Success ||
            _lifetime.Render(surface) != GuideXosResult.Success ||
            _state.Render(surface) != GuideXosResult.Success ||
            _active.Render(surface) != GuideXosResult.Success ||
            _source.Render(surface) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

    private void SyncDetails()
    {
        if (!TryGetSelectedRecord(out GuideXosApplicationSnapshotRecord record,
                out _))
        {
            SetNoSelectionDetails();
            return;
        }

        Span<char> text = stackalloc char[50];
        int position = 0;
        Append(text, ref position, "Name: ".AsSpan());
        CopyDisplayName(ref record, text, ref position);
        _name.SetText(text[..position]);

        position = 0;
        Append(text, ref position, "App ID: ".AsSpan());
        if (record.applicationIdLength == 0u)
        {
            Append(text, ref position, "unavailable".AsSpan());
            _applicationIdContinuation.Clear();
            _applicationIdContinuation2.Clear();
        }
        else
        {
            int firstIdLength = Math.Min((int)record.applicationIdLength, 42);
            CopyApplicationId(ref record, 0u, (uint)firstIdLength,
                text, ref position);
            _applicationIdContinuation.Clear();
            _applicationIdContinuation2.Clear();
            if (firstIdLength < record.applicationIdLength)
            {
                Span<char> continuation = stackalloc char[50];
                int continuationLength = 0;
                uint remaining = record.applicationIdLength - (uint)firstIdLength;
                int copied = Math.Min((int)remaining, continuation.Length);
                CopyApplicationId(ref record,
                    (uint)firstIdLength, (uint)copied,
                    continuation, ref continuationLength);
                _applicationIdContinuation.SetText(
                    continuation[..continuationLength]);
                uint remainingAfterSecond = remaining - (uint)copied;
                if (remainingAfterSecond != 0u)
                {
                    Span<char> continuation2 = stackalloc char[50];
                    int continuation2Length = 0;
                    int copied2 = Math.Min((int)remainingAfterSecond,
                        continuation2.Length);
                    CopyApplicationId(ref record,
                        (uint)(firstIdLength + copied), (uint)copied2,
                        continuation2, ref continuation2Length);
                    _applicationIdContinuation2.SetText(
                        continuation2[..continuation2Length]);
                }
            }
        }
        _applicationId.SetText(text[..position]);

        position = 0;
        Append(text, ref position, "Lifetime: 0x".AsSpan());
        AppendHex64(text, ref position, record.instanceId);
        _lifetime.SetText(text[..position]);

        position = 0;
        Append(text, ref position, "State: ".AsSpan());
        Append(text, ref position, StateName(record.state));
        _state.SetText(text[..position]);

        position = 0;
        Append(text, ref position, "Active: ".AsSpan());
        Append(text, ref position,
            record.IsActive ? "Yes".AsSpan() : "No".AsSpan());
        _active.SetText(text[..position]);

        position = 0;
        Append(text, ref position, "Source: ".AsSpan());
        Append(text, ref position, SourceName(record.source));
        _source.SetText(text[..position]);
    }

    private void SetNoSelectionDetails()
    {
        _name.Clear();
        _applicationId.Clear();
        _applicationIdContinuation.Clear();
        _applicationIdContinuation2.Clear();
        _lifetime.Clear();
        _state.Clear();
        _active.Clear();
        _source.Clear();
    }

    private void SetStatus(ReadOnlySpan<char> text)
    {
        _snapshotStatus.SetText(text[..Math.Min(
            text.Length, _snapshotStatus.MaximumTextLength)]);
    }

    private void SetSnapshotStatus(in GuideXosApplicationSnapshot snapshot)
    {
        Span<char> text = stackalloc char[56];
        int position = 0;
        if (snapshot.IsTruncated)
        {
            Append(text, ref position, "Showing ".AsSpan());
            AppendUnsigned(text, ref position, snapshot.Count);
            Append(text, ref position, " of ".AsSpan());
            AppendUnsigned(text, ref position, snapshot.TotalCount);
            Append(text, ref position, " applications".AsSpan());
        }
        else if (snapshot.Count == 0u)
        {
            Append(text, ref position, "No applications in snapshot".AsSpan());
        }
        else
        {
            Append(text, ref position, "Showing ".AsSpan());
            AppendUnsigned(text, ref position, snapshot.Count);
            Append(text, ref position, " applications".AsSpan());
        }
        SetStatus(text[..position]);
    }

    private static bool TryBuildRow(ref GuideXosApplicationSnapshotRecord record,
        Span<char> storage, out int length)
    {
        length = 0;
        if (storage.Length < 48 || record.displayNameLength >= 32u)
            return false;
        storage[length++] = record.IsActive ? '*' : ' ';
        storage[length++] = ' ';
        if (!CopyDisplayName(ref record, storage, ref length)) return false;
        storage[length++] = ' ';
        storage[length++] = ' ';
        ReadOnlySpan<char> state = StateName(record.state);
        if (state.Length > storage.Length - length) return false;
        state.CopyTo(storage[length..]);
        length += state.Length;
        return length <= 48;
    }

    private static bool CopyDisplayName(
        ref GuideXosApplicationSnapshotRecord record,
        Span<char> destination, ref int position)
    {
        if (record.displayNameLength >
            (uint)(destination.Length - position)) return false;
        fixed (byte* value = record.displayName)
        {
            for (uint index = 0; index < record.displayNameLength; ++index)
                destination[position++] = (char)value[index];
        }
        return true;
    }

    private static bool CopyApplicationId(
        ref GuideXosApplicationSnapshotRecord record,
        uint offset, uint count, Span<char> destination, ref int position)
    {
        if (count > (uint)(destination.Length - position)) return false;
        fixed (byte* value = record.applicationId)
        {
            for (uint index = 0; index < count; ++index)
                destination[position++] = (char)value[offset + index];
        }
        return true;
    }

    private static void Append(Span<char> destination, ref int position,
        ReadOnlySpan<char> text)
    {
        int length = Math.Min(text.Length, destination.Length - position);
        text[..length].CopyTo(destination[position..]);
        position += length;
    }

    private static void AppendUnsigned(Span<char> destination,
        ref int position, uint value)
    {
        Span<char> digits = stackalloc char[10];
        int count = 0;
        do
        {
            digits[count++] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (count != 0 && position < destination.Length)
            destination[position++] = digits[--count];
    }

    private static void AppendHex64(Span<char> destination,
        ref int position, ulong value)
    {
        const string digits = "0123456789ABCDEF";
        for (int shift = 60; shift >= 0 && position < destination.Length;
            shift -= 4)
        {
            destination[position++] = digits[(int)((value >> shift) & 0xFu)];
        }
    }

    private static ReadOnlySpan<char> StateName(
        GuideXosApplicationSnapshotState state) => state switch
    {
        GuideXosApplicationSnapshotState.NotLoaded => "NotLoaded".AsSpan(),
        GuideXosApplicationSnapshotState.Running => "Running".AsSpan(),
        GuideXosApplicationSnapshotState.Suspended => "Suspended".AsSpan(),
        GuideXosApplicationSnapshotState.Terminated => "Terminated".AsSpan(),
        _ => "Unknown".AsSpan(),
    };

    private static ReadOnlySpan<char> SourceName(
        GuideXosApplicationSnapshotSource source) => source switch
    {
        GuideXosApplicationSnapshotSource.AppManagerInstance => "AppManager".AsSpan(),
        GuideXosApplicationSnapshotSource.ShellSurface => "Shell surface".AsSpan(),
        GuideXosApplicationSnapshotSource.ManagedLogicalApplication => "Managed app".AsSpan(),
        _ => "Unknown".AsSpan(),
    };

    private static bool Inside(int x, int y, int left, int top,
        int width, int height) => x >= left && y >= top &&
            x < left + width && y < top + height;
}
#endif
