using System;

namespace HostLogProof;

public enum GuideXosDialogResult
{
    None = 0,
    OK = 1,
    Cancel = 2,
    Yes = 3,
    No = 4,
    Apply = 5,
    Discard = 6,
    Reset = 7,
}

/// <summary>
/// One bounded, non-owning modal overlay. The owner application retains every
/// control; this object stores only direct references and routes them through
/// the existing child GuideXosControlHost scope.
/// </summary>
public sealed class GuideXosDialog
{
    private enum MemberKind : byte
    {
        None = 0,
        Label = 1,
        Button = 2,
        CheckBox = 3,
        ComboBox = 4,
        Separator = 5,
        ListBox = 6,
        TextInput = 7,
    }

    private struct MemberEntry
    {
        public object Control;
        public MemberKind Kind;
        public int Id;
        public GuideXosDialogResult Result;
        public bool Focusable;
    }

    public const int DefaultMaximumMemberCount = 8;
    public const int MaximumSupportedMemberCount = 8;
    public const int MaximumTitleLength = 32;
    public const int MaximumMessageLength = 112;
    public const int MaximumMessageLines = 4;
    public const int MinimumWidth = 240;
    public const int MaximumWidth = 512;
    public const int MinimumHeight = 144;
    public const int MaximumHeight = 288;

    private readonly MemberEntry[] _members;
    private readonly char[] _title = new char[MaximumTitleLength];
    private readonly char[] _message = new char[MaximumMessageLength];
    private readonly int _capacity;
    private readonly int _x;
    private readonly int _y;
    private readonly int _width;
    private readonly int _height;
    private int _titleLength;
    private int _messageLength;
    private int _memberCount;
    private int _defaultButtonId;
    private GuideXosDialogResult _cancelResult = GuideXosDialogResult.Cancel;
    private GuideXosDialogResult _result;
    private GuideXosControlHost _controlHost;
    private GuideXosControlHost _parentHost;
    private bool _isOpen;

    public GuideXosDialog(
        int x,
        int y,
        int width,
        int height,
        string title,
        int maximumMemberCount = DefaultMaximumMemberCount)
    {
        _capacity = maximumMemberCount < 1 ||
            maximumMemberCount > MaximumSupportedMemberCount
                ? DefaultMaximumMemberCount : maximumMemberCount;
        _members = new MemberEntry[_capacity];
        if (!IsValidBounds(x, y, width, height))
        {
            x = 112;
            y = 92;
            width = 336;
            height = 160;
        }
        _x = x;
        _y = y;
        _width = width;
        _height = height;
        _controlHost = new GuideXosControlHost(_capacity);
        TrySetTitle(title ?? string.Empty);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumMemberCount => _capacity;
    public int MemberCount => _memberCount;
    public int RegistrationCount => _controlHost?.RegistrationCount ?? 0;
    public int RegistrationCapacity => _controlHost?.MaximumControlCount ?? 0;
    public bool IsOpen => _isOpen;
    public bool IsModal => _isOpen && _parentHost != null &&
        _parentHost.IsModalActive &&
        ReferenceEquals(_parentHost.ActiveScopeHost, _controlHost);
    public bool HasTransientInputCapture =>
        _controlHost?.HasTransientInputCapture ?? false;
    public bool HasPointerDragCapture =>
        _controlHost?.HasPointerDragCapture ?? false;
    public GuideXosDialogResult Result => _result;
    public string Title => new(_title, 0, _titleLength);
    public string Message => new(_message, 0, _messageLength);
    public int DefaultButtonId => _defaultButtonId;
    public GuideXosDialogResult CancelResult => _cancelResult;
    public GuideXosControlHost ControlHost => _controlHost;
    public Action<GuideXosDialogResult> Closed { get; set; }

    public bool TrySetTitle(string title)
    {
        if (_isOpen || !IsValidAscii(title, MaximumTitleLength, false)) return false;
        Copy(title.AsSpan(), _title, out _titleLength);
        return true;
    }

    public bool TrySetMessage(string message)
    {
        if (_isOpen || !IsValidMessage(message, MessageLineCapacity))
        {
            return false;
        }
        Copy(message.AsSpan(), _message, out _messageLength);
        return true;
    }

    public bool TryAddMember(object control, bool focusable = true)
    {
        if (_isOpen || control == null || _memberCount >= _capacity ||
            TryFindMember(control, out _) || !TryGetMemberKind(control, out MemberKind kind) ||
            !HasNoLayoutOwner(control, kind) ||
            !FitsBounds(control, kind, _x, _y, _width, _height))
        {
            return false;
        }

        int id = _memberCount + 1;
        GuideXosControlHostResult registration = kind switch
        {
            MemberKind.Button => _controlHost.TryRegisterButton(id,
                (GuideXosButton)control, focusable),
            MemberKind.CheckBox => _controlHost.TryRegisterCheckBox(id,
                (GuideXosCheckBox)control, focusable),
            MemberKind.ComboBox => _controlHost.TryRegisterComboBox(id,
                (GuideXosComboBox)control, focusable),
            MemberKind.ListBox => _controlHost.TryRegisterListBox(id,
                (GuideXosListBox)control, focusable),
            MemberKind.TextInput => _controlHost.TryRegisterTextInput(id,
                (GuideXosTextInput)control, focusable),
            _ => GuideXosControlHostResult.Registered,
        };
        if (registration != GuideXosControlHostResult.Registered) return false;

        _members[_memberCount++] = new MemberEntry
        {
            Control = control,
            Kind = kind,
            Id = kind is MemberKind.Button or MemberKind.CheckBox or
                MemberKind.ComboBox or MemberKind.ListBox or MemberKind.TextInput
                ? id : 0,
            Focusable = focusable,
        };
        return true;
    }

    public bool TryRemoveMember(object control)
    {
        if (_isOpen || control == null || !TryFindMember(control, out int index))
            return false;
        MemberEntry entry = _members[index];
        object defaultControl = FindMemberControlById(_defaultButtonId);
        if (ReferenceEquals(defaultControl, control)) defaultControl = null;
        if (entry.Id != 0 &&
            _controlHost.TryUnregister(entry.Id) != GuideXosControlHostResult.Unregistered)
        {
            return false;
        }
        for (int move = index + 1; move < _memberCount; move++)
        {
            _members[move - 1] = _members[move];
        }
        _members[--_memberCount] = default;
        if (!RebuildControlHost()) return false;
        _defaultButtonId = 0;
        if (defaultControl != null && TryFindMember(defaultControl, out int defaultIndex))
            _defaultButtonId = _members[defaultIndex].Id;
        return true;
    }

    public bool TryClearMembers()
    {
        if (_isOpen) return false;
        _controlHost.Reset();
        Array.Clear(_members);
        _memberCount = 0;
        _defaultButtonId = 0;
        _cancelResult = GuideXosDialogResult.Cancel;
        _result = GuideXosDialogResult.None;
        return true;
    }

    public bool TrySetButtonResult(
        GuideXosButton button, GuideXosDialogResult result)
    {
        if (_isOpen || button == null || !IsDefinedResult(result) ||
            !TryFindMember(button, out int index) ||
            _members[index].Kind != MemberKind.Button)
        {
            return false;
        }
        _members[index].Result = result;
        return true;
    }

    public bool TrySetDefaultButton(GuideXosButton button)
    {
        if (_isOpen || button == null || !TryFindMember(button, out int index) ||
            _members[index].Kind != MemberKind.Button || _members[index].Id == 0)
        {
            return false;
        }
        _defaultButtonId = _members[index].Id;
        return true;
    }

    public bool TrySetCancelResult(GuideXosDialogResult result)
    {
        if (_isOpen || !IsDefinedResult(result)) return false;
        _cancelResult = result;
        return true;
    }

    /// <summary>
    /// Enters the existing host modal scope. The child controls are registered
    /// once at association time; repeated opens do not churn or duplicate IDs.
    /// </summary>
    public bool Open(GuideXosControlHost parentHost)
    {
        if (_isOpen || parentHost == null || parentHost.IsModalActive ||
            _controlHost == null || _controlHost.IsModalActive ||
            _controlHost.RegistrationCount == 0 ||
            !parentHost.EnterModal(_controlHost))
        {
            return false;
        }

        int initialId = IsFocusableAndEligible(_defaultButtonId)
            ? _defaultButtonId : FindFirstEligibleMemberId();
        if (initialId == 0 ||
            _controlHost.TryFocus(initialId) != GuideXosControlHostResult.Focused)
        {
            parentHost.ExitModal();
            return false;
        }

        for (int index = 0; index < _memberCount; index++)
        {
            MemberEntry entry = _members[index];
            if (entry.Kind == MemberKind.ComboBox &&
                ((GuideXosComboBox)entry.Control).IsOpen)
            {
                _controlHost.TryFocus(entry.Id);
                if (!_controlHost.TryAcquireTransientInputCapture(entry.Id))
                {
                    parentHost.ExitModal();
                    return false;
                }
            }
        }

        _parentHost = parentHost;
        _result = GuideXosDialogResult.None;
        _isOpen = true;
        return true;
    }

    /// <summary>Closes once, restores the parent scope, then invokes Closed.</summary>
    public bool Close(GuideXosDialogResult result)
    {
        if (!_isOpen || !IsDefinedResult(result)) return false;
        _result = result;
        _isOpen = false;
        GuideXosControlHost parent = _parentHost;
        _parentHost = null;
        if (parent != null && parent.IsModalActive &&
            ReferenceEquals(parent.ActiveScopeHost, _controlHost))
        {
            parent.ExitModal();
        }
        Action<GuideXosDialogResult> callback = Closed;
        callback?.Invoke(_result);
        return true;
    }

    public GuideXosControlHostResult HandleInput(GuideXosInputEvent input)
    {
        if (!_isOpen) return GuideXosControlHostResult.Ignored;

        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            int id = HitMember(input.X, input.Y);
            if (_controlHost.HasTransientInputCapture)
            {
                id = _controlHost.TransientInputCaptureOwnerId;
            }
            else if (!ContainsPoint(input.X, input.Y))
            {
                // The application consumes the event at its modal boundary;
                // no parent control is offered this outside click.
                return GuideXosControlHostResult.Ignored;
            }
            if (id == 0) return GuideXosControlHostResult.Ignored;
            int originX = 0;
            int originY = 0;
            int lineHeight = 18;
            for (int index = 0; index < _memberCount; index++)
            {
                MemberEntry entry = _members[index];
                if (entry.Id == id && entry.Kind == MemberKind.ListBox)
                {
                    GuideXosListBox list = (GuideXosListBox)entry.Control;
                    originX = list.X;
                    originY = list.Y;
                    lineHeight = list.LineHeight;
                    break;
                }
            }
            GuideXosControlHostResult pointer = _parentHost.FocusAndRoutePointer(
                id, input.X, input.Y, originX, originY,
                GuideXosLabel.CharacterWidth, lineHeight);
            CompleteActivatedMember(pointer, _controlHost.ActiveControlId);
            return pointer;
        }

        if (input.Kind == GuideXosInputKind.Wheel)
        {
            int id = _controlHost.HasTransientInputCapture
                ? _controlHost.TransientInputCaptureOwnerId
                : HitWheelMember(input.X, input.Y);
            if (id == 0) return GuideXosControlHostResult.Ignored;
            int originX = 0;
            int originY = 0;
            int lineHeight = 18;
            for (int index = 0; index < _memberCount; index++)
            {
                MemberEntry entry = _members[index];
                if (entry.Id == id && entry.Kind == MemberKind.ListBox)
                {
                    GuideXosListBox list = (GuideXosListBox)entry.Control;
                    originX = list.X;
                    originY = list.Y;
                    lineHeight = list.LineHeight;
                    break;
                }
            }
            return _parentHost.HandleWheel(id, input.X, input.Y,
                input.WheelDelta, originX, originY,
                GuideXosLabel.CharacterWidth, lineHeight);
        }

        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Escape)
        {
            if (_controlHost.HasTransientInputCapture)
            {
                return _parentHost.HandleInput(input);
            }
            GuideXosDialogResult cancel = _cancelResult;
            Close(cancel);
            return GuideXosControlHostResult.Cancelled;
        }

        if (input.Kind == GuideXosInputKind.KeyDown &&
            input.KeyCode == (uint)GuideXosTextInputKey.Enter &&
            _controlHost.ActiveControlKind != GuideXosManagedControlKind.Button &&
            _defaultButtonId != 0)
        {
            _controlHost.TryFocus(_defaultButtonId);
        }

        int activeId = _controlHost.ActiveControlId;
        GuideXosControlHostResult routed = _parentHost.HandleInput(input);
        CompleteActivatedMember(routed, activeId);
        return routed;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_isOpen) return GuideXosResult.Success;
        if (surface.TryFillRect(_x, _y, _width, _height, 0x003D465Au) != GuideXosResult.Success ||
            surface.TryFillRect(_x, _y, _width, 2, 0x00A9C4E8u) != GuideXosResult.Success ||
            surface.TryFillRect(_x, _y + _height - 2, _width, 2, 0x00A9C4E8u) != GuideXosResult.Success ||
            surface.TryFillRect(_x, _y, 2, _height, 0x00A9C4E8u) != GuideXosResult.Success ||
            surface.TryFillRect(_x + _width - 2, _y, 2, _height, 0x00A9C4E8u) != GuideXosResult.Success ||
            surface.TryFillRect(_x + 2, _y + 2, _width - 4, 22, 0x004F6380u) != GuideXosResult.Success ||
            RenderAsciiLine(surface, _x + 10, _y + 4, _title, _titleLength) != GuideXosResult.Success ||
            RenderMessage(surface) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }

        for (int index = 0; index < _memberCount; index++)
        {
            GuideXosResult rendered = RenderMember(surface, _members[index]);
            if (rendered != GuideXosResult.Success) return rendered;
        }
        return GuideXosResult.Success;
    }

    internal bool ContainsPoint(int x, int y) =>
        x >= _x && x < _x + _width && y >= _y && y < _y + _height;

    internal static bool IsValidAscii(string value, int maximumLength,
        bool allowNewLine)
    {
        if (value == null || value.Length > maximumLength) return false;
        for (int index = 0; index < value.Length; index++)
        {
            char character = value[index];
            if (character == '\n' && allowNewLine) continue;
            if (character < 32 || character > 126) return false;
        }
        return true;
    }

    internal static bool IsValidMessage(string value, int lineCapacity) =>
        IsValidAscii(value, MaximumMessageLength, true) && lineCapacity > 0 &&
        CountMessageLines(value.AsSpan(), lineCapacity) <= MaximumMessageLines;

    private int MessageLineCapacity => Math.Min(56, (_width - 24) / 8);

    private static bool IsValidBounds(int x, int y, int width, int height) =>
        x >= 0 && y >= 0 && width >= MinimumWidth && width <= MaximumWidth &&
        height >= MinimumHeight && height <= MaximumHeight &&
        x <= 4095 - width && y <= 4095 - height;

    private static bool IsDefinedResult(GuideXosDialogResult result) =>
        result is GuideXosDialogResult.None or GuideXosDialogResult.OK or
            GuideXosDialogResult.Cancel or GuideXosDialogResult.Yes or
            GuideXosDialogResult.No or GuideXosDialogResult.Apply or
            GuideXosDialogResult.Discard or GuideXosDialogResult.Reset;

    private static void Copy(ReadOnlySpan<char> source, char[] destination,
        out int length)
    {
        source.CopyTo(destination);
        length = source.Length;
    }

    private static int CountMessageLines(ReadOnlySpan<char> value, int capacity)
    {
        if (value.Length == 0) return 1;
        int lines = 0;
        int offset = 0;
        while (offset < value.Length)
        {
            if (value[offset] == '\n')
            {
                lines++;
                offset++;
                continue;
            }
            int count = 0;
            while (offset < value.Length && value[offset] != '\n' && count < capacity)
            {
                offset++;
                count++;
            }
            lines++;
            if (offset < value.Length && value[offset] == '\n') offset++;
        }
        return lines;
    }

    private bool TryFindMember(object control, out int index)
    {
        for (int candidate = 0; candidate < _memberCount; candidate++)
        {
            if (ReferenceEquals(_members[candidate].Control, control))
            {
                index = candidate;
                return true;
            }
        }
        index = -1;
        return false;
    }

    private bool RebuildControlHost()
    {
        _controlHost.Reset();
        for (int index = 0; index < _memberCount; index++)
        {
            MemberEntry entry = _members[index];
            entry.Id = entry.Kind is MemberKind.Button or MemberKind.CheckBox or
                MemberKind.ComboBox or MemberKind.ListBox or MemberKind.TextInput
                ? index + 1 : 0;
            _members[index] = entry;
            GuideXosControlHostResult result = entry.Kind switch
            {
                MemberKind.Button => _controlHost.TryRegisterButton(entry.Id,
                    (GuideXosButton)entry.Control, entry.Focusable),
                MemberKind.CheckBox => _controlHost.TryRegisterCheckBox(entry.Id,
                    (GuideXosCheckBox)entry.Control, entry.Focusable),
                MemberKind.ComboBox => _controlHost.TryRegisterComboBox(entry.Id,
                    (GuideXosComboBox)entry.Control, entry.Focusable),
                MemberKind.ListBox => _controlHost.TryRegisterListBox(entry.Id,
                    (GuideXosListBox)entry.Control, entry.Focusable),
                MemberKind.TextInput => _controlHost.TryRegisterTextInput(entry.Id,
                    (GuideXosTextInput)entry.Control, entry.Focusable),
                _ => GuideXosControlHostResult.Registered,
            };
            if (result != GuideXosControlHostResult.Registered) return false;
        }
        return true;
    }

    private object FindMemberControlById(int id)
    {
        for (int index = 0; index < _memberCount; index++)
        {
            if (_members[index].Id == id) return _members[index].Control;
        }
        return null;
    }

    private static bool TryGetMemberKind(object control, out MemberKind kind)
    {
        kind = control switch
        {
            GuideXosLabel => MemberKind.Label,
            GuideXosButton => MemberKind.Button,
            GuideXosCheckBox => MemberKind.CheckBox,
            GuideXosComboBox => MemberKind.ComboBox,
            GuideXosSeparator => MemberKind.Separator,
            GuideXosListBox => MemberKind.ListBox,
            GuideXosTextInput => MemberKind.TextInput,
            _ => MemberKind.None,
        };
        return kind != MemberKind.None;
    }

    private static bool HasNoLayoutOwner(object control, MemberKind kind) => kind switch
    {
        MemberKind.Label => ((GuideXosLabel)control).ParentPanel == null &&
            ((GuideXosLabel)control).ParentScrollView == null &&
            ((GuideXosLabel)control).ParentGroupBox == null &&
            ((GuideXosLabel)control).VerticalStackOwner == null,
        MemberKind.Button => ((GuideXosButton)control).ParentPanel == null &&
            ((GuideXosButton)control).ParentScrollView == null &&
            ((GuideXosButton)control).ParentGroupBox == null &&
            ((GuideXosButton)control).VerticalStackOwner == null,
        MemberKind.CheckBox => ((GuideXosCheckBox)control).ParentPanel == null &&
            ((GuideXosCheckBox)control).ParentScrollView == null &&
            ((GuideXosCheckBox)control).ParentGroupBox == null &&
            ((GuideXosCheckBox)control).VerticalStackOwner == null,
        MemberKind.ComboBox => ((GuideXosComboBox)control).ParentPanel == null &&
            ((GuideXosComboBox)control).ParentScrollView == null &&
            ((GuideXosComboBox)control).ParentGroupBox == null &&
            ((GuideXosComboBox)control).VerticalStackOwner == null,
        MemberKind.Separator => ((GuideXosSeparator)control).ParentPanel == null &&
            ((GuideXosSeparator)control).ParentScrollView == null &&
            ((GuideXosSeparator)control).ParentGroupBox == null &&
            ((GuideXosSeparator)control).VerticalStackOwner == null,
        MemberKind.ListBox => true,
        MemberKind.TextInput => true,
        _ => false,
    };

    private static bool FitsBounds(object control, MemberKind kind,
        int x, int y, int width, int height)
    {
        int childX = kind switch
        {
            MemberKind.Label => ((GuideXosLabel)control).X,
            MemberKind.Button => ((GuideXosButton)control).X,
            MemberKind.CheckBox => ((GuideXosCheckBox)control).X,
            MemberKind.ComboBox => ((GuideXosComboBox)control).X,
            MemberKind.ListBox => ((GuideXosListBox)control).X,
            MemberKind.TextInput => ((GuideXosTextInput)control).X,
            _ => ((GuideXosSeparator)control).X,
        };
        int childY = kind switch
        {
            MemberKind.Label => ((GuideXosLabel)control).Y,
            MemberKind.Button => ((GuideXosButton)control).Y,
            MemberKind.CheckBox => ((GuideXosCheckBox)control).Y,
            MemberKind.ComboBox => ((GuideXosComboBox)control).Y,
            MemberKind.ListBox => ((GuideXosListBox)control).Y,
            MemberKind.TextInput => ((GuideXosTextInput)control).Y,
            _ => ((GuideXosSeparator)control).Y,
        };
        int childWidth = kind switch
        {
            MemberKind.Label => ((GuideXosLabel)control).Width,
            MemberKind.Button => ((GuideXosButton)control).Width,
            MemberKind.CheckBox => ((GuideXosCheckBox)control).Width,
            MemberKind.ComboBox => ((GuideXosComboBox)control).Width,
            MemberKind.ListBox => ((GuideXosListBox)control).Width,
            MemberKind.TextInput => ((GuideXosTextInput)control).Width,
            _ => ((GuideXosSeparator)control).Width,
        };
        int childHeight = kind switch
        {
            MemberKind.Label => ((GuideXosLabel)control).Height,
            MemberKind.Button => ((GuideXosButton)control).Height,
            MemberKind.CheckBox => ((GuideXosCheckBox)control).Height,
            MemberKind.ComboBox => ((GuideXosComboBox)control).Height,
            MemberKind.ListBox => ((GuideXosListBox)control).Height,
            MemberKind.TextInput => ((GuideXosTextInput)control).Height,
            _ => ((GuideXosSeparator)control).Height,
        };
        return childX >= x && childY >= y && childWidth > 0 && childHeight > 0 &&
            childX + childWidth <= x + width && childY + childHeight <= y + height;
    }

    private int FindFirstEligibleMemberId()
    {
        for (int index = 0; index < _memberCount; index++)
        {
            int id = _members[index].Id;
            if (IsFocusableAndEligible(id)) return id;
        }
        return 0;
    }

    private bool IsFocusableAndEligible(int id)
    {
        if (id <= 0) return false;
        for (int index = 0; index < _memberCount; index++)
        {
            MemberEntry entry = _members[index];
            if (entry.Id != id) continue;
            return entry.Kind switch
            {
                MemberKind.Button => ((GuideXosButton)entry.Control).EffectiveVisible &&
                    ((GuideXosButton)entry.Control).EffectiveEnabled,
                MemberKind.CheckBox => ((GuideXosCheckBox)entry.Control).EffectiveVisible &&
                    ((GuideXosCheckBox)entry.Control).EffectiveEnabled,
                MemberKind.ComboBox => ((GuideXosComboBox)entry.Control).EffectiveVisible &&
                    ((GuideXosComboBox)entry.Control).EffectiveEnabled,
                MemberKind.ListBox => ((GuideXosListBox)entry.Control).EffectiveVisible &&
                    ((GuideXosListBox)entry.Control).Enabled,
                MemberKind.TextInput => ((GuideXosTextInput)entry.Control).IsVisible &&
                    ((GuideXosTextInput)entry.Control).IsEnabled,
                _ => false,
            };
        }
        return false;
    }

    private int HitMember(int x, int y)
    {
        for (int index = _memberCount - 1; index >= 0; index--)
        {
            MemberEntry entry = _members[index];
            if (entry.Id == 0 || !IsMemberVisible(entry)) continue;
            bool hit = entry.Kind switch
            {
                MemberKind.Button => Contains(((GuideXosButton)entry.Control), x, y),
                MemberKind.CheckBox => Contains(((GuideXosCheckBox)entry.Control), x, y),
                MemberKind.ComboBox => Contains(((GuideXosComboBox)entry.Control), x, y),
                MemberKind.ListBox => Contains(((GuideXosListBox)entry.Control), x, y),
                MemberKind.TextInput => Contains(((GuideXosTextInput)entry.Control), x, y),
                _ => false,
            };
            if (hit) return entry.Id;
        }
        return 0;
    }

    private static bool Contains(GuideXosButton control, int x, int y) =>
        x >= control.X && x < control.X + control.Width &&
        y >= control.Y && y < control.Y + control.Height;

    private static bool Contains(GuideXosCheckBox control, int x, int y) =>
        x >= control.X && x < control.X + control.Width &&
        y >= control.Y && y < control.Y + control.Height;

    private static bool Contains(GuideXosComboBox control, int x, int y) =>
        control.ContainsPoint(x, y) || control.ContainsDropDownPoint(x, y);

    private static bool Contains(GuideXosListBox control, int x, int y) =>
        x >= control.X && x < control.X + control.Width &&
        y >= control.Y && y < control.Y + control.Height;

    private static bool Contains(GuideXosTextInput control, int x, int y) =>
        x >= control.X && x < control.X + control.Width &&
        y >= control.Y && y < control.Y + control.Height;

    private int HitWheelMember(int x, int y)
    {
        for (int index = _memberCount - 1; index >= 0; index--)
        {
            MemberEntry entry = _members[index];
            if (entry.Id == 0 || entry.Kind != MemberKind.ListBox ||
                !IsMemberVisible(entry)) continue;
            if (Contains((GuideXosListBox)entry.Control, x, y)) return entry.Id;
        }
        return 0;
    }

    private bool IsMemberVisible(MemberEntry entry) => entry.Kind switch
    {
        MemberKind.Label => ((GuideXosLabel)entry.Control).EffectiveVisible,
        MemberKind.Button => ((GuideXosButton)entry.Control).EffectiveVisible,
        MemberKind.CheckBox => ((GuideXosCheckBox)entry.Control).EffectiveVisible,
        MemberKind.ComboBox => ((GuideXosComboBox)entry.Control).EffectiveVisible,
        MemberKind.Separator => ((GuideXosSeparator)entry.Control).Visible,
        MemberKind.ListBox => ((GuideXosListBox)entry.Control).EffectiveVisible,
        MemberKind.TextInput => ((GuideXosTextInput)entry.Control).IsVisible,
        _ => false,
    };

    private void CompleteActivatedMember(GuideXosControlHostResult result, int id)
    {
        if (result != GuideXosControlHostResult.Activated || id <= 0) return;
        for (int index = 0; index < _memberCount; index++)
        {
            if (_members[index].Id == id &&
                _members[index].Kind == MemberKind.Button &&
                _members[index].Result != GuideXosDialogResult.None)
            {
                Close(_members[index].Result);
                return;
            }
        }
    }

    private GuideXosResult RenderMessage(GuideXosSurface surface)
    {
        int capacity = MessageLineCapacity;
        int offset = 0;
        int line = 0;
        Span<byte> bytes = stackalloc byte[56];
        while (line < MaximumMessageLines && offset < _messageLength)
        {
            int count = 0;
            while (offset < _messageLength && _message[offset] != '\n' && count < capacity)
                bytes[count++] = (byte)_message[offset++];
            if (offset < _messageLength && _message[offset] == '\n') offset++;
            if (count != 0 && surface.TrySetText(_x + 12,
                    _y + 34 + line * 18, bytes[..count]) != GuideXosResult.Success)
                return GuideXosResult.InvalidArgument;
            line++;
        }
        return GuideXosResult.Success;
    }

    private static GuideXosResult RenderAsciiLine(GuideXosSurface surface,
        int x, int y, char[] chars, int length)
    {
        Span<byte> bytes = stackalloc byte[MaximumTitleLength];
        for (int index = 0; index < length; index++) bytes[index] = (byte)chars[index];
        return surface.TrySetText(x, y, bytes[..length]);
    }

    private static GuideXosResult RenderMember(GuideXosSurface surface,
        MemberEntry entry) => entry.Kind switch
    {
        MemberKind.Label => ((GuideXosLabel)entry.Control).Render(surface),
        MemberKind.Button => ((GuideXosButton)entry.Control).Render(surface),
        MemberKind.CheckBox => ((GuideXosCheckBox)entry.Control).Render(surface),
        MemberKind.ComboBox => ((GuideXosComboBox)entry.Control).Render(surface),
        MemberKind.Separator => ((GuideXosSeparator)entry.Control).Render(surface),
        MemberKind.ListBox => ((GuideXosListBox)entry.Control).Render(
            surface, ((GuideXosListBox)entry.Control).X,
            ((GuideXosListBox)entry.Control).Y,
            ((GuideXosListBox)entry.Control).LineHeight),
        MemberKind.TextInput => ((GuideXosTextInput)entry.Control).Render(
            surface, ((GuideXosTextInput)entry.Control).X,
            ((GuideXosTextInput)entry.Control).Y,
            ((GuideXosTextInput)entry.Control).LabelUtf8),
        _ => GuideXosResult.InvalidArgument,
    };
}
