namespace HostLogProof;

public enum GuideXosManagedControlKind
{
    None = 0,
    Button = 1,
    TextInput = 2,
    TextArea = 3,
    ListBox = 4,
    CheckBox = 5,
    RadioButton = 6,
}

public enum GuideXosControlHostResult
{
    Ignored = 0,
    Registered = 1,
    Focused = 2,
    Traversed = 3,
    Changed = 4,
    Moved = 5,
    Activated = 6,
    Submitted = 7,
    Cancelled = 8,
    Rejected = 9,
    Disabled = 10,
    Toggled = 11,
}

/// <summary>
/// Fixed-capacity focus and input owner for one managed control scope.
/// Control behavior stays in the four reusable controls; this host owns only
/// registration, focus order, scope transitions, and event routing.
/// </summary>
public sealed class GuideXosControlHost
{
    public const int MaximumSupportedControlCount = 8;

    private struct ControlEntry
    {
        public int Id;
        public GuideXosManagedControlKind Kind;
        public object Control;
        public bool Focusable;
    }

    private readonly ControlEntry[] _entries;
    private readonly int _capacity;
    private int _registrationCount;
    private int _activeIndex = -1;
    private GuideXosControlHost _modalHost;
    private int _savedActiveId;
    private int _savedActiveIndex = -1;

    public GuideXosControlHost(int maximumControlCount = MaximumSupportedControlCount)
    {
        if (maximumControlCount < 1 ||
            maximumControlCount > MaximumSupportedControlCount)
        {
            maximumControlCount = MaximumSupportedControlCount;
        }
        _capacity = maximumControlCount;
        _entries = new ControlEntry[maximumControlCount];
    }

    public int MaximumControlCount => _capacity;
    public int RegistrationCount => _registrationCount;
    public int ActiveIndex => _activeIndex;
    public int ActiveControlId =>
        _activeIndex >= 0 && _activeIndex < _registrationCount
            ? _entries[_activeIndex].Id : 0;
    public GuideXosManagedControlKind ActiveControlKind =>
        _activeIndex >= 0 && _activeIndex < _registrationCount
            ? _entries[_activeIndex].Kind : GuideXosManagedControlKind.None;
    public bool IsModalActive => _modalHost != null;
    public GuideXosControlHost ActiveScopeHost => _modalHost ?? this;

    public GuideXosControlHostResult TryRegisterButton(
        int id, GuideXosButton control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.Button, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterTextInput(
        int id, GuideXosTextInput control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.TextInput, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterTextArea(
        int id, GuideXosTextArea control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.TextArea, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterListBox(
        int id, GuideXosListBox control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.ListBox, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterCheckBox(
        int id, GuideXosCheckBox control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.CheckBox, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterRadioButton(
        int id, GuideXosRadioButton control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.RadioButton, control, focusable);
    }

    public GuideXosControlHostResult TrySetFocusable(int id, bool focusable)
    {
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }
        _entries[index].Focusable = focusable;
        NormalizeActiveFocus();
        return GuideXosControlHostResult.Focused;
    }

    public GuideXosControlHostResult TryFocus(int id)
    {
        if (_modalHost != null)
        {
            return _modalHost.TryFocus(id);
        }
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }
        if (!IsEligible(index))
        {
            return IsControlDisabled(index)
                ? GuideXosControlHostResult.Disabled
                : GuideXosControlHostResult.Rejected;
        }
        return FocusIndex(index);
    }

    public GuideXosControlHostResult FocusAndRoutePointer(
        int id,
        int x,
        int y,
        int originX = 0,
        int originY = 0,
        int characterWidth = 8,
        int lineHeight = 18)
    {
        if (_modalHost != null)
        {
            return _modalHost.FocusAndRoutePointer(
                id, x, y, originX, originY, characterWidth, lineHeight);
        }
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }
        if (_entries[index].Kind == GuideXosManagedControlKind.RadioButton &&
            !((GuideXosRadioButton)_entries[index].Control).ContainsPoint(x, y))
        {
            return GuideXosControlHostResult.Ignored;
        }
        if (!IsEligible(index))
        {
            return IsControlDisabled(index)
                ? GuideXosControlHostResult.Disabled
                : GuideXosControlHostResult.Rejected;
        }
        GuideXosControlHostResult focusResult = FocusIndex(index);
        if (focusResult != GuideXosControlHostResult.Focused)
        {
            return focusResult;
        }
        return RoutePointer(index, x, y, originX, originY,
            characterWidth, lineHeight);
    }

    public GuideXosControlHostResult HandleInput(GuideXosInputEvent input)
    {
        return input.Kind switch
        {
            GuideXosInputKind.KeyDown => HandleKey(
                (GuideXosTextInputKey)input.KeyCode, input.Shift),
            GuideXosInputKind.KeyChar => HandleCharacter(input.Character),
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    public GuideXosControlHostResult HandleKey(
        GuideXosTextInputKey key, bool shift = false)
    {
        if (_modalHost != null)
        {
            return _modalHost.HandleKey(key, shift);
        }
        NormalizeActiveFocus();
        if (key == GuideXosTextInputKey.Tab)
        {
            return Traverse(shift);
        }
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        return RouteKey(_activeIndex, key, shift);
    }

    public GuideXosControlHostResult HandleCharacter(char character)
    {
        if (_modalHost != null)
        {
            return _modalHost.HandleCharacter(character);
        }
        NormalizeActiveFocus();
        if (character == '\t') return GuideXosControlHostResult.Ignored;
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        return RouteCharacter(_activeIndex, character);
    }

    public bool EnterModal(GuideXosControlHost modalHost)
    {
        if (modalHost == null || modalHost == this || _modalHost != null)
        {
            return false;
        }
        NormalizeActiveFocus();
        _savedActiveIndex = _activeIndex;
        _savedActiveId = ActiveControlId;
        BlurAll();
        _activeIndex = -1;
        _modalHost = modalHost;
        return true;
    }

    public bool ExitModal()
    {
        if (_modalHost == null) return false;
        _modalHost.ClearFocus();
        _modalHost = null;

        int savedIndex = -1;
        bool restored = _savedActiveId != 0 &&
            TryFindIndex(_savedActiveId, out savedIndex) &&
            IsEligible(savedIndex);
        if (restored)
        {
            FocusIndex(savedIndex);
        }
        else
        {
            int start = _savedActiveIndex < 0 ? 0 : _savedActiveIndex + 1;
            int next = FindEligibleFrom(start, false);
            if (next >= 0) FocusIndex(next);
            else _activeIndex = -1;
        }
        _savedActiveId = 0;
        _savedActiveIndex = -1;
        return true;
    }

    public void Reset()
    {
        if (_modalHost != null) _modalHost.Reset();
        BlurAll();
        for (int index = 0; index < _entries.Length; index++)
        {
            _entries[index] = default;
        }
        _registrationCount = 0;
        _activeIndex = -1;
        _modalHost = null;
        _savedActiveId = 0;
        _savedActiveIndex = -1;
    }

    private GuideXosControlHostResult TryRegister(
        int id, GuideXosManagedControlKind kind, object control, bool focusable)
    {
        if (id <= 0 || kind == GuideXosManagedControlKind.None ||
            control == null || _registrationCount >= _capacity ||
            HasId(id) || !MatchesKind(kind, control))
        {
            return GuideXosControlHostResult.Rejected;
        }
        _entries[_registrationCount++] = new ControlEntry
        {
            Id = id,
            Kind = kind,
            Control = control,
            Focusable = focusable,
        };
        return GuideXosControlHostResult.Registered;
    }

    private GuideXosControlHostResult Traverse(bool reverse)
    {
        int start;
        if (_activeIndex < 0)
        {
            start = reverse ? _registrationCount - 1 : 0;
        }
        else
        {
            start = _activeIndex + (reverse ? -1 : 1);
        }

        int eligible = FindEligibleFrom(start, reverse);
        if (eligible < 0) return GuideXosControlHostResult.Ignored;
        if (eligible == _activeIndex) return GuideXosControlHostResult.Traversed;
        return FocusIndex(eligible) == GuideXosControlHostResult.Focused
            ? GuideXosControlHostResult.Traversed
            : GuideXosControlHostResult.Ignored;
    }

    private int FindEligibleFrom(int start, bool reverse)
    {
        if (_registrationCount == 0) return -1;
        int index = start;
        for (int count = 0; count < _registrationCount; count++)
        {
            if (index < 0) index = _registrationCount - 1;
            if (index >= _registrationCount) index = 0;
            if (IsEligible(index)) return index;
            index += reverse ? -1 : 1;
        }
        return -1;
    }

    private void NormalizeActiveFocus()
    {
        if (_activeIndex < 0 || IsEligible(_activeIndex)) return;
        int next = FindEligibleFrom(_activeIndex + 1, false);
        if (next >= 0) FocusIndex(next);
        else
        {
            BlurAll();
            _activeIndex = -1;
        }
    }

    private GuideXosControlHostResult FocusIndex(int index)
    {
        if (index < 0 || index >= _registrationCount || !IsEligible(index))
        {
            return GuideXosControlHostResult.Rejected;
        }
        if (_activeIndex == index && IsControlFocused(index))
        {
            return GuideXosControlHostResult.Focused;
        }
        BlurAll();
        _activeIndex = index;
        FocusControl(index);
        return GuideXosControlHostResult.Focused;
    }

    private void ClearFocus()
    {
        BlurAll();
        _activeIndex = -1;
    }

    private void BlurAll()
    {
        for (int index = 0; index < _registrationCount; index++)
        {
            BlurControl(index);
        }
    }

    private GuideXosControlHostResult RoutePointer(
        int index,
        int x,
        int y,
        int originX,
        int originY,
        int characterWidth,
        int lineHeight)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button => Map(
                ((GuideXosButton)_entries[index].Control).HandlePointerDown(x, y)),
            GuideXosManagedControlKind.TextInput => Map(
                ((GuideXosTextInput)_entries[index].Control).HandlePointerDown(x, y)),
            GuideXosManagedControlKind.TextArea => Map(
                ((GuideXosTextArea)_entries[index].Control).HandlePointerDown(
                    x, y, originX, originY, characterWidth, lineHeight)),
            GuideXosManagedControlKind.ListBox => Map(
                ((GuideXosListBox)_entries[index].Control).HandlePointerDown(
                    x, y, originX, originY, characterWidth, lineHeight)),
            GuideXosManagedControlKind.CheckBox => Map(
                ((GuideXosCheckBox)_entries[index].Control).HandlePointerDown(x, y)),
            GuideXosManagedControlKind.RadioButton => Map(
                ((GuideXosRadioButton)_entries[index].Control).HandlePointerDown(x, y)),
            _ => GuideXosControlHostResult.Rejected,
        };
    }

    private GuideXosControlHostResult RouteKey(
        int index, GuideXosTextInputKey key, bool shift)
    {
        if (_entries[index].Kind == GuideXosManagedControlKind.RadioButton)
        {
            GuideXosRadioButton radio =
                (GuideXosRadioButton)_entries[index].Control;
            GuideXosRadioButtonResult result = radio.HandleKey(key);
            if (result == GuideXosRadioButtonResult.Moved)
            {
                GuideXosRadioButton target = radio.Group?.GetMember(
                    radio.RequestedGroupIndex);
                if (target == null || !TryFindControlReference(target,
                        out int targetHostIndex))
                {
                    return GuideXosControlHostResult.Rejected;
                }
                return FocusIndex(targetHostIndex) ==
                    GuideXosControlHostResult.Focused
                    ? GuideXosControlHostResult.Moved
                    : GuideXosControlHostResult.Rejected;
            }
            return Map(result);
        }
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button => Map(
                ((GuideXosButton)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.TextInput => Map(
                ((GuideXosTextInput)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.TextArea => Map(
                ((GuideXosTextArea)_entries[index].Control).HandleKey(key, shift)),
            GuideXosManagedControlKind.ListBox => Map(
                ((GuideXosListBox)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.CheckBox => Map(
                ((GuideXosCheckBox)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.RadioButton => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Rejected,
        };
    }

    private GuideXosControlHostResult RouteCharacter(int index, char character)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button => Map(
                ((GuideXosButton)_entries[index].Control).HandleCharacter(character)),
            GuideXosManagedControlKind.TextInput => Map(
                ((GuideXosTextInput)_entries[index].Control).HandleCharacter(character)),
            GuideXosManagedControlKind.TextArea => Map(
                ((GuideXosTextArea)_entries[index].Control).HandleCharacter(character)),
            GuideXosManagedControlKind.CheckBox => Map(
                ((GuideXosCheckBox)_entries[index].Control).HandleCharacter(character)),
            GuideXosManagedControlKind.RadioButton => Map(
                ((GuideXosRadioButton)_entries[index].Control).HandleCharacter(character)),
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private bool IsEligible(int index)
    {
        return index >= 0 && index < _registrationCount &&
            _entries[index].Focusable && !IsControlDisabled(index);
    }

    private bool IsControlDisabled(int index)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button =>
                !((GuideXosButton)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.CheckBox =>
                !((GuideXosCheckBox)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.RadioButton =>
                !((GuideXosRadioButton)_entries[index].Control).Enabled,
            _ => false,
        };
    }

    private bool IsControlFocused(int index)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button =>
                ((GuideXosButton)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.TextInput =>
                ((GuideXosTextInput)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.TextArea =>
                ((GuideXosTextArea)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.ListBox =>
                ((GuideXosListBox)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.CheckBox =>
                ((GuideXosCheckBox)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.RadioButton =>
                ((GuideXosRadioButton)_entries[index].Control).IsFocused,
            _ => false,
        };
    }

    private void FocusControl(int index)
    {
        switch (_entries[index].Kind)
        {
            case GuideXosManagedControlKind.Button:
                ((GuideXosButton)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.TextInput:
                ((GuideXosTextInput)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.TextArea:
                ((GuideXosTextArea)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.ListBox:
                ((GuideXosListBox)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.CheckBox:
                ((GuideXosCheckBox)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.RadioButton:
                ((GuideXosRadioButton)_entries[index].Control).Focus();
                break;
        }
    }

    private void BlurControl(int index)
    {
        switch (_entries[index].Kind)
        {
            case GuideXosManagedControlKind.Button:
                ((GuideXosButton)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.TextInput:
                ((GuideXosTextInput)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.TextArea:
                ((GuideXosTextArea)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.ListBox:
                ((GuideXosListBox)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.CheckBox:
                ((GuideXosCheckBox)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.RadioButton:
                ((GuideXosRadioButton)_entries[index].Control).Blur();
                break;
        }
    }

    private bool HasId(int id)
    {
        return TryFindIndex(id, out _);
    }

    private bool TryFindIndex(int id, out int index)
    {
        for (int candidate = 0; candidate < _registrationCount; candidate++)
        {
            if (_entries[candidate].Id == id)
            {
                index = candidate;
                return true;
            }
        }
        index = -1;
        return false;
    }

    private static bool MatchesKind(
        GuideXosManagedControlKind kind, object control)
    {
        return kind switch
        {
            GuideXosManagedControlKind.Button => control is GuideXosButton,
            GuideXosManagedControlKind.TextInput => control is GuideXosTextInput,
            GuideXosManagedControlKind.TextArea => control is GuideXosTextArea,
            GuideXosManagedControlKind.ListBox => control is GuideXosListBox,
            GuideXosManagedControlKind.CheckBox => control is GuideXosCheckBox,
            GuideXosManagedControlKind.RadioButton => control is GuideXosRadioButton,
            _ => false,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosButtonResult result)
    {
        return result switch
        {
            GuideXosButtonResult.Activated => GuideXosControlHostResult.Activated,
            GuideXosButtonResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosButtonResult.Ignored => GuideXosControlHostResult.Ignored,
            _ => GuideXosControlHostResult.Rejected,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosCheckBoxResult result)
    {
        return result switch
        {
            GuideXosCheckBoxResult.Toggled => GuideXosControlHostResult.Toggled,
            GuideXosCheckBoxResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosCheckBoxResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosCheckBoxResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosRadioButtonResult result)
    {
        return result switch
        {
            GuideXosRadioButtonResult.Selected => GuideXosControlHostResult.Changed,
            GuideXosRadioButtonResult.Moved => GuideXosControlHostResult.Moved,
            GuideXosRadioButtonResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosRadioButtonResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosRadioButtonResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(
        GuideXosTextInputEditResult result)
    {
        return result switch
        {
            GuideXosTextInputEditResult.Changed => GuideXosControlHostResult.Changed,
            GuideXosTextInputEditResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosTextInputEditResult.Submitted => GuideXosControlHostResult.Submitted,
            GuideXosTextInputEditResult.Cancelled => GuideXosControlHostResult.Cancelled,
            GuideXosTextInputEditResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(
        GuideXosTextAreaEditResult result)
    {
        return result switch
        {
            GuideXosTextAreaEditResult.Changed => GuideXosControlHostResult.Changed,
            GuideXosTextAreaEditResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosTextAreaEditResult.Moved => GuideXosControlHostResult.Moved,
            GuideXosTextAreaEditResult.Submitted => GuideXosControlHostResult.Submitted,
            GuideXosTextAreaEditResult.Cancelled => GuideXosControlHostResult.Cancelled,
            GuideXosTextAreaEditResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosListBoxResult result)
    {
        return result switch
        {
            GuideXosListBoxResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosListBoxResult.SelectionChanged => GuideXosControlHostResult.Changed,
            GuideXosListBoxResult.Activated => GuideXosControlHostResult.Activated,
            GuideXosListBoxResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private bool TryFindControlReference(
        GuideXosRadioButton target, out int index)
    {
        for (int candidate = 0; candidate < _registrationCount; candidate++)
        {
            if (ReferenceEquals(_entries[candidate].Control, target))
            {
                index = candidate;
                return true;
            }
        }
        index = -1;
        return false;
    }
}
