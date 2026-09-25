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
    ComboBox = 7,
    PopupMenu = 8,
    ScrollBar = 9,
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
    Pending = 13,
    Released = 14,
    Scrolled = 15,
    DragStarted = 16,
    Dragged = 17,
    DragEnded = 18,
    Paged = 19,
}

/// <summary>
/// Fixed-capacity focus and input owner for one managed control scope.
/// Control behavior stays in the four reusable controls; this host owns only
/// registration, focus order, scope transitions, and event routing.
/// </summary>
public sealed class GuideXosControlHost
{
    public const int MaximumSupportedControlCount = 10;
    public const int DefaultMaximumControlCount = 8;

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
    // One bounded transient owner. The registered control remains focused;
    // this lease only gives its open transient state first refusal of input.
    private int _transientCaptureIndex = -1;
    // One bounded primary-pointer drag owner. This lease is separate from,
    // and mutually exclusive with, transient popup capture.
    private int _pointerDragIndex = -1;
    // Space activation is intentionally split across KeyDown and KeyChar by
    // the existing managed input transport. Keep only the target identity for
    // that one in-flight gesture so lifecycle changes can cancel its commit.
    private int _pendingSpaceIndex = -1;
    private bool _cancelledSpaceCharacter;
    // Secondary context gestures are split across native button down/up.
    // Store the stable control ID and press coordinates, never a mutable
    // registration index, so membership changes cannot retarget the release.
    private int _pendingSecondaryControlId;
    private int _pendingSecondaryX;
    private int _pendingSecondaryY;

    public GuideXosControlHost(int maximumControlCount = DefaultMaximumControlCount)
    {
        if (maximumControlCount < 1 ||
            maximumControlCount > MaximumSupportedControlCount)
        {
            maximumControlCount = DefaultMaximumControlCount;
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
    public bool HasTransientInputCapture
    {
        get
        {
            ReconcileTransientCapture();
            return _transientCaptureIndex >= 0;
        }
    }
    public int TransientInputCaptureOwnerId
    {
        get
        {
            ReconcileTransientCapture();
            return _transientCaptureIndex >= 0
                ? _entries[_transientCaptureIndex].Id : 0;
        }
    }
    public GuideXosManagedControlKind TransientInputCaptureKind
    {
        get
        {
            ReconcileTransientCapture();
            return _transientCaptureIndex >= 0
                ? _entries[_transientCaptureIndex].Kind
                : GuideXosManagedControlKind.None;
        }
    }

    public bool HasPointerDragCapture
    {
        get
        {
            ReconcilePointerDrag();
            return _pointerDragIndex >= 0;
        }
    }
    public int PointerDragCaptureOwnerId
    {
        get
        {
            ReconcilePointerDrag();
            return _pointerDragIndex >= 0
                ? _entries[_pointerDragIndex].Id : 0;
        }
    }
    public GuideXosManagedControlKind PointerDragCaptureKind
    {
        get
        {
            ReconcilePointerDrag();
            return _pointerDragIndex >= 0
                ? _entries[_pointerDragIndex].Kind
                : GuideXosManagedControlKind.None;
        }
    }

    public bool HasPendingSecondaryPointer => _pendingSecondaryControlId != 0;
    public int PendingSecondaryPointerTargetId => _pendingSecondaryControlId;

    /// <summary>
    /// Starts a secondary-button gesture without changing managed focus. An
    /// existing transient owner gets first refusal and consumes the gesture.
    /// </summary>
    public GuideXosControlHostResult BeginSecondaryPointerGesture(
        int id, int x, int y)
    {
        if (_modalHost != null)
        {
            return _modalHost.BeginSecondaryPointerGesture(id, x, y);
        }
        CancelPendingSpace();
        CancelPendingSecondaryPointer();
        if (TryGetTransientCaptureIndex(out int capturedIndex))
        {
            return RoutePointerAndCapture(capturedIndex, x, y, 0, 0, 8, 18);
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
        _pendingSecondaryControlId = id;
        _pendingSecondaryX = x;
        _pendingSecondaryY = y;
        return GuideXosControlHostResult.Pending;
    }

    /// <summary>
    /// Completes the pending secondary gesture only for the original target.
    /// Coordinates from the press are returned for deterministic menu
    /// placement; a stale or invalid release is consumed and cannot fall
    /// through to another control.
    /// </summary>
    public GuideXosControlHostResult CompleteSecondaryPointerGesture(
        out int targetId, out int x, out int y)
    {
        targetId = _pendingSecondaryControlId;
        x = _pendingSecondaryX;
        y = _pendingSecondaryY;
        if (_modalHost != null)
        {
            _modalHost.CancelPendingSecondaryPointer();
            targetId = 0;
            x = 0;
            y = 0;
            return GuideXosControlHostResult.Cancelled;
        }
        if (targetId == 0)
        {
            return GuideXosControlHostResult.Ignored;
        }
        _pendingSecondaryControlId = 0;
        _pendingSecondaryX = 0;
        _pendingSecondaryY = 0;
        if (TryGetTransientCaptureIndex(out _))
        {
            targetId = 0;
            x = 0;
            y = 0;
            return GuideXosControlHostResult.Cancelled;
        }
        if (!TryFindIndex(targetId, out int index) || !IsEligible(index))
        {
            targetId = 0;
            x = 0;
            y = 0;
            return GuideXosControlHostResult.Cancelled;
        }
        return GuideXosControlHostResult.Released;
    }

    public void CancelPendingSecondaryPointer()
    {
        _pendingSecondaryControlId = 0;
        _pendingSecondaryX = 0;
        _pendingSecondaryY = 0;
    }

    /// <summary>
    /// Acquires the single transient-input lease for an already-open,
    /// registered transient popup. A second owner is rejected; ownership never
    /// transfers implicitly and there is no capture stack.
    /// </summary>
    public bool TryAcquireTransientInputCapture(int id)
    {
        if (_modalHost != null)
        {
            return _modalHost.TryAcquireTransientInputCapture(id);
        }
        if (HasPointerDragCapture) return false;
        if (!TryFindIndex(id, out int index) ||
            !IsOpenTransientCandidate(index)) return false;
        ReconcileTransientCapture();
        if (_transientCaptureIndex >= 0 && _transientCaptureIndex != index)
        {
            return false;
        }
        _transientCaptureIndex = index;
        return true;
    }

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

    public GuideXosControlHostResult TryRegisterComboBox(
        int id, GuideXosComboBox control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.ComboBox, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterPopupMenu(
        int id, GuideXosPopupMenu control, bool focusable = false)
    {
        return TryRegister(id, GuideXosManagedControlKind.PopupMenu, control, focusable);
    }

    public GuideXosControlHostResult TryRegisterScrollBar(
        int id, GuideXosScrollBar control, bool focusable = true)
    {
        return TryRegister(id, GuideXosManagedControlKind.ScrollBar, control, focusable);
    }

    /// <summary>Routes motion to the single active primary-pointer drag owner.</summary>
    public GuideXosControlHostResult HandlePointerMove(int x, int y)
    {
        if (_modalHost != null || !TryGetPointerDragIndex(out int index))
        {
            return GuideXosControlHostResult.Ignored;
        }
        GuideXosScrollBarResult result =
            ((GuideXosScrollBar)_entries[index].Control).HandlePointerMove(x, y);
        ReconcilePointerDrag();
        return Map(result);
    }

    /// <summary>Completes the active primary-button drag, if any.</summary>
    public GuideXosControlHostResult HandlePointerUp(
        int x, int y, GuideXosPointerButton button = GuideXosPointerButton.Primary)
    {
        if (button != GuideXosPointerButton.Primary ||
            !TryGetPointerDragIndex(out int index))
        {
            return GuideXosControlHostResult.Ignored;
        }
        GuideXosScrollBarResult result =
            ((GuideXosScrollBar)_entries[index].Control).HandlePointerUp(x, y, button);
        _pointerDragIndex = -1;
        return Map(result);
    }

    public void CancelPointerDrag()
    {
        if (_pointerDragIndex >= 0 &&
            _pointerDragIndex < _registrationCount &&
            _entries[_pointerDragIndex].Kind == GuideXosManagedControlKind.ScrollBar)
        {
            ((GuideXosScrollBar)_entries[_pointerDragIndex].Control).CancelDrag();
        }
        _pointerDragIndex = -1;
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
        if (_pendingSecondaryControlId == id)
        {
            CancelPendingSecondaryPointer();
        }
        if (!TryFindIndex(id, out int index))
        {
            return GuideXosControlHostResult.Rejected;
        }

        if (_transientCaptureIndex == index)
        {
            ReleaseTransientCapture();
        }
        else if (_transientCaptureIndex > index)
        {
            _transientCaptureIndex--;
        }
        if (_pointerDragIndex == index)
        {
            CancelPointerDrag();
        }
        else if (_pointerDragIndex > index)
        {
            _pointerDragIndex--;
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
        CancelPendingSecondaryIfInvalid();
        NormalizeActiveFocus();
        ReconcileTransientCapture();
        ReconcilePointerDrag();
        return GuideXosControlHostResult.Focused;
    }

    public GuideXosControlHostResult TryFocus(int id)
    {
        if (_modalHost != null)
        {
            return _modalHost.TryFocus(id);
        }
        CancelPendingSpace();
        CancelPendingSecondaryPointer();
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
        if (HasPointerDragCapture) return GuideXosControlHostResult.Ignored;
        CancelPendingSpace();
        CancelPendingSecondaryPointer();
        if (TryGetTransientCaptureIndex(out int capturedIndex))
        {
            // The transient drop-down owns the complete pointer gesture. An
            // outside click closes and is consumed; it cannot fall through to
            // the control whose rectangle was hit underneath the popup.
            return RoutePointerAndCapture(capturedIndex, x, y, originX, originY,
                characterWidth, lineHeight);
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
        // A pointer hit is already expressed in the control's current
        // viewport. Preserve a scrolled ListBox viewport while assigning host
        // focus; ordinary keyboard/programmatic focus still reconciles the
        // selected row through FocusIndex.
        GuideXosControlHostResult focusResult =
            _entries[index].Kind == GuideXosManagedControlKind.ListBox
                ? FocusIndexForPointer(index)
                : FocusIndex(index);
        if (focusResult != GuideXosControlHostResult.Focused)
        {
            return focusResult;
        }
        return RoutePointerAndCapture(index, x, y, originX, originY,
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

    /// <summary>
    /// Routes a wheel event to the eligible control under the pointer. A
    /// transient owner gets first refusal; an open popup therefore swallows
    /// the event instead of allowing wheel-through to a background control.
    /// Wheel never changes focus or selection by itself.
    /// </summary>
    public GuideXosControlHostResult HandleWheel(
        int id,
        int x,
        int y,
        int wheelDelta,
        int originX = 0,
        int originY = 0,
        int characterWidth = 8,
        int lineHeight = 18)
    {
        if (_modalHost != null)
        {
            return _modalHost.HandleWheel(
                id, x, y, wheelDelta, originX, originY,
                characterWidth, lineHeight);
        }
        if (wheelDelta == 0) return GuideXosControlHostResult.Ignored;
        if (HasPointerDragCapture) return GuideXosControlHostResult.Ignored;
        if (TryGetTransientCaptureIndex(out _))
        {
            return GuideXosControlHostResult.Ignored;
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
        return RouteWheel(index, x, y, wheelDelta, originX, originY,
            characterWidth, lineHeight);
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
        if (key == GuideXosTextInputKey.Tab)
        {
            if (TryGetTransientCaptureIndex(out int transientIndex))
            {
                CloseTransientControl(transientIndex);
                ReleaseTransientCapture();
            }
            NormalizeActiveFocus();
            return Traverse(shift);
        }
        if (TryGetTransientCaptureIndex(out int capturedIndex))
        {
            GuideXosControlHostResult capturedResult =
                RouteKey(capturedIndex, key, shift);
            ReconcileTransientCapture();
            if (key == (GuideXosTextInputKey)' ' &&
                capturedResult == GuideXosControlHostResult.Ignored &&
                IsSpaceActivationControl(capturedIndex) &&
                IsEligible(capturedIndex) && IsControlFocused(capturedIndex))
            {
                _pendingSpaceIndex = capturedIndex;
            }
            return capturedResult;
        }
        NormalizeActiveFocus();
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        int routedIndex = _activeIndex;
        GuideXosControlHostResult result = RouteKey(routedIndex, key, shift);
        CaptureIfOpen(routedIndex);
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
        if (TryGetTransientCaptureIndex(out int capturedIndex))
        {
            GuideXosControlHostResult capturedResult =
                RouteCharacter(capturedIndex, character);
            ReconcileTransientCapture();
            return capturedResult;
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
            GuideXosControlHostResult pendingResult =
                RouteCharacter(pendingIndex, character);
            CaptureIfOpen(pendingIndex);
            return pendingResult;
        }
        NormalizeActiveFocus();
        if (character == '\t') return GuideXosControlHostResult.Ignored;
        if (_activeIndex < 0) return GuideXosControlHostResult.Ignored;
        GuideXosControlHostResult result = RouteCharacter(_activeIndex, character);
        CaptureIfOpen(_activeIndex);
        return result;
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
        CancelPendingSecondaryIfInvalid();
        NormalizeActiveFocus();
        ReconcileTransientCapture();
        ReconcilePointerDrag();
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
        CancelPendingSecondaryPointer();
        CancelPointerDrag();
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
        CancelPendingSecondaryPointer();
        CancelPointerDrag();
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
        CancelPendingSecondaryPointer();
        CancelPointerDrag();
        ReleaseTransientCapture();
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

    private GuideXosControlHostResult FocusIndexForPointer(int index)
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
        ((GuideXosListBox)_entries[index].Control).Focus(false);
        return GuideXosControlHostResult.Focused;
    }

    private void ClearFocus()
    {
        CancelPendingSpace();
        CancelPendingSecondaryPointer();
        BlurAll();
        _activeIndex = -1;
    }

    private void BlurAll()
    {
        for (int index = 0; index < _registrationCount; index++)
        {
            BlurControl(index);
        }
        ReleaseTransientCapture();
    }

    private static bool IsSpaceActivationControl(
        GuideXosManagedControlKind kind)
    {
        return kind == GuideXosManagedControlKind.Button ||
            kind == GuideXosManagedControlKind.CheckBox ||
            kind == GuideXosManagedControlKind.RadioButton ||
            kind == GuideXosManagedControlKind.ComboBox;
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

    private void CancelPendingSecondaryIfInvalid()
    {
        if (_pendingSecondaryControlId == 0) return;
        if (TryFindIndex(_pendingSecondaryControlId, out int index) &&
            IsEligible(index)) return;
        CancelPendingSecondaryPointer();
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
            GuideXosManagedControlKind.ComboBox => Map(
                ((GuideXosComboBox)_entries[index].Control).HandlePointerDown(x, y)),
            GuideXosManagedControlKind.PopupMenu => Map(
                ((GuideXosPopupMenu)_entries[index].Control).HandlePointerDown(x, y)),
            GuideXosManagedControlKind.ScrollBar => Map(
                ((GuideXosScrollBar)_entries[index].Control).HandlePointerDown(x, y)),
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
            GuideXosManagedControlKind.ComboBox => Map(
                ((GuideXosComboBox)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.PopupMenu => Map(
                ((GuideXosPopupMenu)_entries[index].Control).HandleKey(key)),
            GuideXosManagedControlKind.ScrollBar => Map(
                ((GuideXosScrollBar)_entries[index].Control).HandleKey(key)),
            _ => GuideXosControlHostResult.Rejected,
        };
    }

    private GuideXosControlHostResult RouteWheel(
        int index,
        int x,
        int y,
        int wheelDelta,
        int originX,
        int originY,
        int characterWidth,
        int lineHeight)
    {
        return _entries[index].Kind switch
        {
            GuideXosManagedControlKind.TextArea => Map(
                ((GuideXosTextArea)_entries[index].Control).HandleWheel(
                    wheelDelta)),
            GuideXosManagedControlKind.ListBox => Map(
                ((GuideXosListBox)_entries[index].Control).HandleWheel(
                    wheelDelta)),
            GuideXosManagedControlKind.ScrollBar => Map(
                ((GuideXosScrollBar)_entries[index].Control).HandleWheel(
                    wheelDelta)),
            _ => GuideXosControlHostResult.Ignored,
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
            GuideXosManagedControlKind.ComboBox => Map(
                ((GuideXosComboBox)_entries[index].Control).HandleCharacter(character)),
            GuideXosManagedControlKind.PopupMenu => Map(
                ((GuideXosPopupMenu)_entries[index].Control).HandleCharacter(character)),
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
            GuideXosManagedControlKind.ComboBox =>
                ((GuideXosComboBox)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.PopupMenu =>
                ((GuideXosPopupMenu)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.ScrollBar =>
                ((GuideXosScrollBar)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.TextArea =>
                ((GuideXosTextArea)_entries[index].Control).EffectiveVisible,
            GuideXosManagedControlKind.ListBox =>
                ((GuideXosListBox)_entries[index].Control).EffectiveVisible,
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
            GuideXosManagedControlKind.ComboBox =>
                !((GuideXosComboBox)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.PopupMenu =>
                !((GuideXosPopupMenu)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.TextArea =>
                !((GuideXosTextArea)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.ListBox =>
                !((GuideXosListBox)_entries[index].Control).Enabled,
            GuideXosManagedControlKind.ScrollBar =>
                !((GuideXosScrollBar)_entries[index].Control).Enabled,
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
            GuideXosManagedControlKind.ComboBox =>
                ((GuideXosComboBox)_entries[index].Control).IsFocused,
            GuideXosManagedControlKind.ScrollBar =>
                ((GuideXosScrollBar)_entries[index].Control).IsFocused,
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
            case GuideXosManagedControlKind.ComboBox:
                ((GuideXosComboBox)_entries[index].Control).Focus();
                break;
            case GuideXosManagedControlKind.ScrollBar:
                ((GuideXosScrollBar)_entries[index].Control).Focus();
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
            case GuideXosManagedControlKind.ComboBox:
                ((GuideXosComboBox)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.PopupMenu:
                ((GuideXosPopupMenu)_entries[index].Control).Blur();
                break;
            case GuideXosManagedControlKind.ScrollBar:
                ((GuideXosScrollBar)_entries[index].Control).Blur();
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
            GuideXosManagedControlKind.ComboBox => control is GuideXosComboBox,
            GuideXosManagedControlKind.PopupMenu => control is GuideXosPopupMenu,
            GuideXosManagedControlKind.ScrollBar => control is GuideXosScrollBar,
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

    private static GuideXosControlHostResult Map(GuideXosComboBoxResult result)
    {
        return result switch
        {
            GuideXosComboBoxResult.Opened => GuideXosControlHostResult.Activated,
            GuideXosComboBoxResult.Moved => GuideXosControlHostResult.Moved,
            GuideXosComboBoxResult.SelectionChanged => GuideXosControlHostResult.Changed,
            GuideXosComboBoxResult.Closed => GuideXosControlHostResult.Cancelled,
            GuideXosComboBoxResult.Cancelled => GuideXosControlHostResult.Cancelled,
            GuideXosComboBoxResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosComboBoxResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosComboBoxResult.Rejected => GuideXosControlHostResult.Rejected,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosPopupMenuResult result)
    {
        return result switch
        {
            GuideXosPopupMenuResult.Opened => GuideXosControlHostResult.Activated,
            GuideXosPopupMenuResult.Moved => GuideXosControlHostResult.Moved,
            GuideXosPopupMenuResult.Activated => GuideXosControlHostResult.Activated,
            GuideXosPopupMenuResult.Closed => GuideXosControlHostResult.Cancelled,
            GuideXosPopupMenuResult.Cancelled => GuideXosControlHostResult.Cancelled,
            GuideXosPopupMenuResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosPopupMenuResult.Rejected => GuideXosControlHostResult.Rejected,
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
            GuideXosTextAreaEditResult.Scrolled => GuideXosControlHostResult.Scrolled,
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
            GuideXosListBoxResult.Scrolled => GuideXosControlHostResult.Scrolled,
            _ => GuideXosControlHostResult.Ignored,
        };
    }

    private static GuideXosControlHostResult Map(GuideXosScrollBarResult result)
    {
        return result switch
        {
            GuideXosScrollBarResult.Changed => GuideXosControlHostResult.Changed,
            GuideXosScrollBarResult.Focused => GuideXosControlHostResult.Focused,
            GuideXosScrollBarResult.Disabled => GuideXosControlHostResult.Disabled,
            GuideXosScrollBarResult.Rejected => GuideXosControlHostResult.Rejected,
            GuideXosScrollBarResult.DragStarted => GuideXosControlHostResult.DragStarted,
            GuideXosScrollBarResult.Dragged => GuideXosControlHostResult.Dragged,
            GuideXosScrollBarResult.DragEnded => GuideXosControlHostResult.DragEnded,
            GuideXosScrollBarResult.Cancelled => GuideXosControlHostResult.Cancelled,
            GuideXosScrollBarResult.Paged => GuideXosControlHostResult.Paged,
            GuideXosScrollBarResult.Stepped => GuideXosControlHostResult.Changed,
            GuideXosScrollBarResult.Scrolled => GuideXosControlHostResult.Scrolled,
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

    private bool TryFindControlReference(
        GuideXosComboBox target, out int index)
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

    private bool IsOpenTransientCandidate(int index)
    {
        if (index < 0 || index >= _registrationCount) return false;
        if (_entries[index].Kind == GuideXosManagedControlKind.PopupMenu)
        {
            GuideXosPopupMenu menu =
                (GuideXosPopupMenu)_entries[index].Control;
            return menu.IsOpen && menu.EffectiveVisible && menu.Enabled &&
                menu.InvokerAvailable;
        }
        return IsComboBoxOpen(index) && IsEligible(index) &&
            IsControlFocused(index);
    }

    private bool TryGetTransientCaptureIndex(out int index)
    {
        ReconcileTransientCapture();
        if (_transientCaptureIndex >= 0)
        {
            index = _transientCaptureIndex;
            return true;
        }
        // A control may have opened itself programmatically. Acquire its
        // lease at the next host event, in registration order, without ever
        // creating a capture stack or transferring an existing lease.
        for (int candidate = 0; candidate < _registrationCount; candidate++)
        {
            if (IsOpenTransientCandidate(candidate))
            {
                _transientCaptureIndex = candidate;
                index = candidate;
                return true;
            }
        }
        index = -1;
        return false;
    }

    private void CaptureIfOpen(int index)
    {
        if (index >= 0 && index < _registrationCount &&
            IsOpenTransientCandidate(index))
        {
            // The opening event establishes the lease before it returns to
            // native input. A second owner is never implicitly transferred.
            if (_transientCaptureIndex < 0 ||
                _transientCaptureIndex == index)
            {
                _transientCaptureIndex = index;
            }
        }
        else
        {
            ReconcileTransientCapture();
        }
    }

    private GuideXosControlHostResult RoutePointerAndCapture(
        int index,
        int x,
        int y,
        int originX,
        int originY,
        int characterWidth,
        int lineHeight)
    {
        GuideXosControlHostResult result = RoutePointer(
            index, x, y, originX, originY, characterWidth, lineHeight);
        if (result == GuideXosControlHostResult.DragStarted &&
            _entries[index].Kind == GuideXosManagedControlKind.ScrollBar)
        {
            _pointerDragIndex = index;
        }
        CaptureIfOpen(index);
        return result;
    }

    private bool TryGetPointerDragIndex(out int index)
    {
        ReconcilePointerDrag();
        index = _pointerDragIndex;
        return index >= 0;
    }

    private void ReconcilePointerDrag()
    {
        if (_pointerDragIndex < 0) return;
        if (_modalHost != null ||
            _pointerDragIndex >= _registrationCount ||
            _entries[_pointerDragIndex].Kind != GuideXosManagedControlKind.ScrollBar)
        {
            CancelPointerDrag();
            return;
        }
        GuideXosScrollBar bar =
            (GuideXosScrollBar)_entries[_pointerDragIndex].Control;
        if (!bar.IsDragging || !bar.Visible || !bar.Enabled)
        {
            CancelPointerDrag();
        }
    }

    private void ReconcileTransientCapture()
    {
        if (_transientCaptureIndex < 0) return;
        if (!IsOpenTransientCandidate(_transientCaptureIndex))
        {
            ReleaseTransientCapture();
        }
    }

    private void ReleaseTransientCapture()
    {
        _transientCaptureIndex = -1;
    }

    private bool IsComboBoxOpen(int index)
    {
        return index >= 0 && index < _registrationCount &&
            _entries[index].Kind == GuideXosManagedControlKind.ComboBox &&
            ((GuideXosComboBox)_entries[index].Control).IsOpen;
    }

    private void CloseTransientControl(int index)
    {
        if (index < 0 || index >= _registrationCount) return;
        switch (_entries[index].Kind)
        {
            case GuideXosManagedControlKind.ComboBox:
                ((GuideXosComboBox)_entries[index].Control).Close();
                break;
            case GuideXosManagedControlKind.PopupMenu:
                ((GuideXosPopupMenu)_entries[index].Control).Cancel();
                break;
        }
    }
}
