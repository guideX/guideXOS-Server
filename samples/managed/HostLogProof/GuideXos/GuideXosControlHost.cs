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
    Unregistered = 12,
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
    // Space activation is intentionally split across KeyDown and KeyChar by
    // the existing managed input transport. Keep only the target identity for
    // that one in-flight gesture so lifecycle changes can cancel its commit.
    private int _pendingSpaceIndex = -1;
    private bool _cancelledSpaceCharacter;

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

    /// <summary>
    /// Removes one registered control and, for a radio member, removes its
    /// logical group membership too. This keeps a closed or stale host entry
    /// from participating in a later selection transaction.
    /// </summary>
    public GuideXosControlHostResult TryUnregister(int id)
    {
        if (_modalHost != null) return _modalHost.TryUnregister(id);
        CancelPendingSpace();
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }

        int priorActiveIndex = _activeIndex;
        BlurControl(index);
        if (_entries[index].Kind == GuideXosManagedControlKind.RadioButton)
        {
            GuideXosRadioButton radio =
                (GuideXosRadioButton)_entries[index].Control;
            radio.Group?.TryUnregister(radio);
        }
        for (int move = index + 1; move < _registrationCount; move++)
        {
            _entries[move - 1] = _entries[move];
        }
        _entries[--_registrationCount] = default;

        if (priorActiveIndex == index)
        {
            _activeIndex = -1;
            int next = FindEligibleFrom(index, false);
            if (next >= 0) FocusIndex(next);
        }
        else if (priorActiveIndex > index)
        {
            _activeIndex = priorActiveIndex - 1;
        }
        return GuideXosControlHostResult.Unregistered;
    }

    public GuideXosControlHostResult TrySetFocusable(int id, bool focusable)
    {
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }
        _entries[index].Focusable = focusable;
        CancelPendingSpaceIfInvalid();
        NormalizeActiveFocus();
        return GuideXosControlHostResult.Focused;
    }

    public GuideXosControlHostResult TryFocus(int id)
    {
        if (_modalHost != null)
        {
            return _modalHost.TryFocus(id);
        }
        CancelPendingSpace();
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
        CancelPendingSpace();
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
            if (key == (GuideXosTextInputKey)' ')
            {
                // A Space KeyDown delivered to the modal starts a new modal
                // gesture rather than committing the cancelled background one.
                _cancelledSpaceCharacter = false;
            }
            return _modalHost.HandleKey(key, shift);
        }
        if (key != (GuideXosTextInputKey)' ')
        {
            _cancelledSpaceCharacter = false;
            CancelPendingSpace();
        }
        else
        {
            // A new KeyDown Space starts a fresh transport gesture. It may
            // replace a previous repeat, but must not inherit its cancellation.
            _cancelledSpaceCharacter = false;
        }
        NormalizeActiveFocus();
        if (key == GuideXosTextInputKey.Tab)
        {
            return Traverse(shift);
        }
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        int routedIndex = _activeIndex;
        GuideXosControlHostResult result = RouteKey(routedIndex, key, shift);
        if (key == (GuideXosTextInputKey)' ' &&
            result == GuideXosControlHostResult.Ignored &&
            IsSpaceActivationControl(routedIndex) &&
            IsEligible(routedIndex) && IsControlFocused(routedIndex))
        {
            // An already-selected RadioButton is idempotent. There is no
            // delayed selection to protect in that case, so do not retain a
            // pending gesture that a later lifecycle transition would need
            // to cancel.
            if (_entries[routedIndex].Kind !=
                    GuideXosManagedControlKind.RadioButton ||
                !((GuideXosRadioButton)_entries[routedIndex].Control).Checked)
            {
                _pendingSpaceIndex = routedIndex;
            }
        }
        return result;
    }

    public GuideXosControlHostResult HandleCharacter(char character)
    {
        if (_modalHost != null)
        {
            if (character == ' ' && _cancelledSpaceCharacter)
            {
                // Consume a background Space that arrives after modal entry;
                // it must not activate a modal target by accident.
                _cancelledSpaceCharacter = false;
                return GuideXosControlHostResult.Ignored;
            }
            return _modalHost.HandleCharacter(character);
        }
        if (character == ' ' && _cancelledSpaceCharacter)
        {
            // Consume the character belonging to a gesture cancelled by a
            // visibility, membership, focus, or modal transition. It must not
            // be rerouted to the fallback control selected during recovery.
            _cancelledSpaceCharacter = false;
            return GuideXosControlHostResult.Ignored;
        }
        if (character != ' ')
        {
            _cancelledSpaceCharacter = false;
            CancelPendingSpace();
        }
        else if (_pendingSpaceIndex >= 0)
        {
            int pendingIndex = _pendingSpaceIndex;
            if (_activeIndex != pendingIndex || !IsEligible(pendingIndex) ||
                !IsControlFocused(pendingIndex))
            {
                CancelPendingSpace();
                // This is the stale character itself; consume it now while the
                // cancellation flag protects any later lifecycle transition.
                _cancelledSpaceCharacter = false;
                RecoverFocusAfterCancelledSpace(pendingIndex);
                return GuideXosControlHostResult.Ignored;
            }
            _pendingSpaceIndex = -1;
            _cancelledSpaceCharacter = false;
            return RouteCharacter(pendingIndex, character);
        }
        NormalizeActiveFocus();
        if (character == '\t') return GuideXosControlHostResult.Ignored;
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        return RouteCharacter(_activeIndex, character);
    }

    /// <summary>
    /// Reconciles focus after an application-owned visibility or membership
    /// change. The host remains the only focus authority; showing a control
    /// never selects it automatically.
    /// </summary>
    public GuideXosControlHostResult RefreshVisibility()
    {
        if (_modalHost != null)
        {
            _modalHost.RefreshVisibility();
            return GuideXosControlHostResult.Ignored;
        }
        int priorIndex = _activeIndex;
        CancelPendingSpaceIfInvalid();
        NormalizeActiveFocus();
        return priorIndex != _activeIndex && _activeIndex >= 0
            ? GuideXosControlHostResult.Focused
            : GuideXosControlHostResult.Ignored;
    }

    public bool EnterModal(GuideXosControlHost modalHost)
    {
        if (modalHost == null || modalHost == this || _modalHost != null)
        {
            return false;
        }
        CancelPendingSpace();
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
        CancelPendingSpace();
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
        for (int index = 0; index < _registrationCount; index++)
        {
            if (_entries[index].Kind == GuideXosManagedControlKind.RadioButton)
            {
                GuideXosRadioButton radio =
                    (GuideXosRadioButton)_entries[index].Control;
                radio.Group?.TryUnregister(radio);
            }
        }
        for (int index = 0; index < _entries.Length; index++)
        {
            _entries[index] = default;
        }
        _registrationCount = 0;
        _activeIndex = -1;
        _modalHost = null;
        _savedActiveId = 0;
        _savedActiveIndex = -1;
        _pendingSpaceIndex = -1;
        _cancelledSpaceCharacter = false;
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
        if (_activeIndex < 0) return;
        if (IsEligible(_activeIndex) && IsControlFocused(_activeIndex)) return;
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
        CancelPendingSpace();
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

    private static bool IsSpaceActivationControl(
        GuideXosManagedControlKind kind)
    {
        return kind == GuideXosManagedControlKind.Button ||
            kind == GuideXosManagedControlKind.CheckBox ||
            kind == GuideXosManagedControlKind.RadioButton;
    }

    private bool IsSpaceActivationControl(int index)
    {
        return index >= 0 && index < _registrationCount &&
            IsSpaceActivationControl(_entries[index].Kind);
    }

    private void CancelPendingSpace()
    {
        if (_pendingSpaceIndex >= 0)
        {
            _cancelledSpaceCharacter = true;
        }
        _pendingSpaceIndex = -1;
    }

    private void CancelPendingSpaceIfInvalid()
    {
        if (_pendingSpaceIndex < 0) return;
        int pendingIndex = _pendingSpaceIndex;
        if (_activeIndex == pendingIndex && IsEligible(pendingIndex) &&
            IsControlFocused(pendingIndex)) return;
        CancelPendingSpace();
        RecoverFocusAfterCancelledSpace(pendingIndex);
    }

    private void RecoverFocusAfterCancelledSpace(int cancelledIndex)
    {
        if (_activeIndex != cancelledIndex) return;
        BlurAll();
        _activeIndex = -1;
        int next = FindEligibleFrom(cancelledIndex + 1, false);
        if (next >= 0 && next != cancelledIndex)
        {
            FocusIndex(next);
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
            bool isArrow = key == GuideXosTextInputKey.Left ||
                key == GuideXosTextInputKey.Up ||
                key == GuideXosTextInputKey.Right ||
                key == GuideXosTextInputKey.Down;
            if (isArrow && radio.Group != null)
            {
                bool reverse = key == GuideXosTextInputKey.Left ||
                    key == GuideXosTextInputKey.Up;
                if (!radio.Group.TryGetMoveTarget(radio, reverse,
                        out int targetGroupIndex))
                {
                    return GuideXosControlHostResult.Ignored;
                }
                GuideXosRadioButton target =
                    radio.Group.GetMember(targetGroupIndex);
                if (target == null || !TryFindControlReference(target,
                        out int targetHostIndex))
                {
                    // A group member not registered in this host is never an
                    // eligible arrow-navigation target.
                    return GuideXosControlHostResult.Ignored;
                }
                GuideXosRadioButtonResult moved = radio.HandleKey(key);
                if (moved != GuideXosRadioButtonResult.Moved)
                {
                    return Map(moved);
                }
                return FocusIndex(targetHostIndex) ==
                    GuideXosControlHostResult.Focused
                    ? GuideXosControlHostResult.Moved
                    : GuideXosControlHostResult.Rejected;
            }
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
            _entries[index].Focusable && IsControlVisible(index) &&
            !IsControlDisabled(index);
    }

    private bool IsControlVisible(int index)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.Button =>
                ((GuideXosButton)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.CheckBox =>
                ((GuideXosCheckBox)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.RadioButton =>
                ((GuideXosRadioButton)_entries[index].Control).EffectiveVisible,
            _ => true,
        };
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
