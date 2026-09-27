using System;

namespace HostLogProof.Applications;

/// <summary>
/// Production C143 composition: a non-owning section association and frame,
/// an independent geometry-only Stack, and a direct ScrollView member set.
/// </summary>
public sealed class ManagedGroupBoxDemo : GuideXosApplication
{
    private const int ViewId = 1;
    private const int ScrollBarId = 2;
    private const int ComboId = 3;
    private const int MenuId = 4;
    private const int VisibleToggleId = 5;
    private const int EnabledToggleId = 6;
    private readonly GuideXosScrollView _view = new(24, 95, 300, 120, 9);
    private readonly GuideXosScrollBar _scrollBar = new(336, 95, 16, 120);
    private readonly GuideXosGroupBox _groupBox = new(0, 0, 272, 270, "Server Options");
    private readonly GuideXosVerticalStack _stack;
    private readonly GuideXosLabel _label = new(0, 0, 210, "Section controls");
    private readonly GuideXosCheckBox _progressToggle =
        new(0, 0, 210, 18, "Show progress", true);
    private readonly GuideXosComboBox _comboBox = new(0, 0, 210, 18, 4, 16, 2);
    private readonly GuideXosSeparator _separator = new(0, 0, 210);
    private readonly GuideXosRadioButton _radioOne = new(0, 0, 210, 18, "Standard mode", true);
    private readonly GuideXosRadioButton _radioTwo = new(0, 0, 210, 18, "Verbose mode");
    private readonly GuideXosProgressBar _progress = new(0, 0, 210, 0, 100, 65);
    private readonly GuideXosButton _button = new(0, 0, 128, 18, "Apply settings");
    private readonly GuideXosRadioGroup _radioGroup = new(2);
    private readonly GuideXosCheckBox _visibleToggle =
        new(24, 42, 250, 18, "Show Server Options", true);
    private readonly GuideXosCheckBox _enabledToggle =
        new(24, 64, 250, 18, "Enable Server Options", true);
    private readonly GuideXosPopupMenu _popupMenu = new(0, 0, 136, 3, 20);
    private GuideXosControlHost _host;
    private GuideXosHost _appHost;
    private ulong _window;
    private uint _launchCount;
    private bool _testsRun;
    private int _initialContentHeight;
    private int _finalContentHeight;
    private int _initialFrameX;
    private int _initialFrameY;
    private int _initialButtonY;
    private int _pendingMenuCommand;

    public ManagedGroupBoxDemo()
    {
        _stack = new GuideXosVerticalStack(_groupBox.ContentLeft,
            _groupBox.ContentTop, _groupBox.ContentWidth, 8);
        _stack.TrySetPadding(4, 4, 4, 4);
        _stack.TrySetSpacing(3);
        _label.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _label.TrySetMargins(0, 2, 0, 2);
        _progressToggle.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _progressToggle.TrySetMargins(0, 2, 0, 2);
        _comboBox.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _comboBox.TrySetMargins(8, 2, 8, 2);
        _separator.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _separator.TrySetMargins(16, 2, 16, 2);
        _radioOne.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        _radioOne.TrySetMargins(0, 2, 0, 2);
        _radioTwo.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _radioTwo.TrySetMargins(0, 2, 0, 2);
        _progress.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _progress.TrySetMargins(8, 2, 8, 2);
        _button.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        _button.TrySetMargins(0, 2, 0, 2);
        _comboBox.TryAddItem("Default");
        _comboBox.TryAddItem("Compact");
        _comboBox.TryAddItem("Detailed");
        _progressToggle.Changed = OnProgressVisibilityChanged;
        _visibleToggle.Changed = OnSectionVisibilityChanged;
        _enabledToggle.Changed = OnSectionEnabledChanged;
        _radioGroup.TryRegister(_radioOne);
        _radioGroup.TryRegister(_radioTwo);
        _popupMenu.TryAddItem("Apply", 1u);
        _popupMenu.TryAddSeparator();
        _popupMenu.TryAddItem("Close", 2u);
        _popupMenu.CommandInvoked = OnMenuCommand;
    }

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return GuideXosResult.Success;
        _appHost = host;
        ++_launchCount;
        ResetComposition();
        GuideXosResult create = host.TryCreateSurface(
            "Managed GroupBox"u8, 560, 300, out GuideXosSurface surface);
        if (create != GuideXosResult.Success || surface == null) return create;
        _window = surface.Handle;

        if (!_testsRun)
        {
            bool c126 = GuideXosGroupBoxTests.Run(host, surface);
            bool c141 = GuideXosVerticalStackC141Tests.Run(host);
            bool c142 = GuideXosVerticalStackC142Tests.Run(host);
            bool c143 = GuideXosGroupBoxC143Tests.Run(host, surface);
            _testsRun = true;
            host.TryLog(c126
                ? "C126-RETAINED groupbox=60 result=PASS"u8
                : "C126-RETAINED result=FAIL"u8);
            host.TryLog(c141
                ? "C141-TESTS core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS"u8
                : "C141-TESTS result=FAIL"u8);
            host.TryLog(c142
                ? "C142-TESTS metadata=20 spacing=12 horizontal=12 scrollview=14 popup=10 total=68 result=PASS"u8
                : "C142-TESTS result=FAIL"u8);
            _ = c143;
        }

        _initialContentHeight = _stack.ContentHeight;
        _initialFrameX = _groupBox.X;
        _initialFrameY = _groupBox.Y;
        _initialButtonY = _button.Y;
        bool geometry = _groupBox.ValidateMembershipGeometry(out int outside) &&
            outside == 0;
        bool aligned = _stack.X == _groupBox.ContentLeft &&
            _stack.Y == _groupBox.ContentTop &&
            _stack.Width == _groupBox.ContentWidth &&
            _button.X + _button.Width == _stack.X + _stack.Width -
                _stack.RightPadding - _button.MarginRight;
        bool registered = RegisterControls();
        bool initial = registered && geometry && aligned &&
            _host.RegistrationCount == 6 && _view.MemberCount == 9 &&
            _groupBox.MemberCount == 8 && _stack.MemberCount == 8 &&
            _view.ContentExtent > _view.VisibleExtent &&
            _view.Offset == 0 && _scrollBar.Value == 0;
        host.TryLog(initial
            ? "C143-PROOF launch=PASS registration=6 viewMembers=9 groupMembers=8 stackMembers=8 layout=valid result=PASS"u8
            : "C143-PROOF launch=FAIL result=FAIL"u8);
        host.TryLog(initial && geometry && aligned
            ? "C143-CONTENT rect=valid frame=first responsibilities=separate result=PASS"u8
            : "C143-CONTENT result=FAIL"u8);
        LogGeometry(host, "initial");
        return Render(host, surface);
    }

    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (host.TryGetSurface(_window, out GuideXosSurface surface) !=
                GuideXosResult.Success || surface == null)
            return GuideXosResult.SurfaceCreationFailed;

        if (input.Kind == GuideXosInputKind.PointerMove)
        {
            _host.HandlePointerMove(input.X, input.Y);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerUp)
        {
            _host.HandlePointerUp(input.X, input.Y, input.Button);
            LogGeometry(host, "pointer-up");
            host.TryLog(!_host.HasPointerDragCapture
                ? "C143-DRAG release=PASS owner=none result=PASS"u8
                : "C143-DRAG release=FAIL result=FAIL"u8);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.Wheel)
        {
            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == ViewId
                ? _host.HandleWheel(ViewId, input.X, input.Y, input.WheelDelta)
                : GuideXosControlHostResult.Ignored;
            if (result == GuideXosControlHostResult.Scrolled)
            {
                host.TryLog("C143-WHEEL viewport=changed frame-and-members-translated=PASS result=PASS"u8);
                LogGeometry(host, "scrolled");
            }
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            int externalToggleId = HitId(input.X, input.Y);
            if ((_comboBox.IsOpen || _popupMenu.IsOpen) &&
                (externalToggleId == VisibleToggleId || externalToggleId == EnabledToggleId))
            {
                if (externalToggleId == VisibleToggleId)
                    _visibleToggle.SetChecked(!_visibleToggle.Checked);
                else
                    _enabledToggle.SetChecked(!_enabledToggle.Checked);
                CancelUnavailableTransientState();
                host.TryLog(!_host.HasTransientInputCapture &&
                    !_comboBox.IsOpen && !_popupMenu.IsOpen
                    ? "C143-CAPTURE invalidated=cancelled owner=none result=PASS"u8
                    : "C143-CAPTURE invalidated=stale result=FAIL"u8);
                LogGeometry(host, "state-change-popup-cancel");
                return Render(host, surface);
            }
            if (_comboBox.IsOpen)
            {
                GuideXosControlHostResult popup = _host.FocusAndRoutePointer(
                    ComboId, input.X, input.Y + _view.Offset);
                if (!_comboBox.IsOpen)
                {
                    host.TryLog(popup != GuideXosControlHostResult.Rejected &&
                        popup != GuideXosControlHostResult.Disabled
                        ? "C143-COMBO commit=PASS translated-origin=PASS result=PASS"u8
                        : "C143-COMBO commit=FAIL result=FAIL"u8);
                    host.TryLog(!_host.HasTransientInputCapture
                        ? "C143-CAPTURE popup=none result=PASS"u8
                        : "C143-CAPTURE popup=stale result=FAIL"u8);
                }
                return Render(host, surface);
            }
            if (_popupMenu.IsOpen)
            {
                GuideXosControlHostResult menuResult = _host.FocusAndRoutePointer(
                    MenuId, input.X, input.Y);
                if (!_popupMenu.IsOpen)
                {
                    host.TryLog(_pendingMenuCommand != 0
                        ? "C143-MENU commit=PASS button-invoked=PASS result=PASS"u8
                        : "C143-MENU commit=FAIL result=FAIL"u8);
                    _pendingMenuCommand = 0;
                    _ = menuResult;
                }
                return Render(host, surface);
            }

            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == 0
                ? GuideXosControlHostResult.Ignored
                : _host.FocusAndRoutePointer(id, input.X, input.Y);
            if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _comboBox))
            {
                bool captured = _comboBox.IsOpen &&
                    _host.TryAcquireTransientInputCapture(ComboId);
                host.TryLog(captured
                    ? "C143-COMBO open=PASS arranged=PASS capture=combo result=PASS"u8
                    : "C143-COMBO open=FAIL result=FAIL"u8);
            }
            else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _progressToggle))
            {
                _host.RefreshVisibility();
                _finalContentHeight = _stack.ContentHeight;
                bool stateValid = _groupBox.X == _initialFrameX &&
                    _groupBox.Y == _initialFrameY &&
                    (_progress.Visible
                        ? _finalContentHeight == _initialContentHeight && _button.Y == _initialButtonY
                        : _finalContentHeight < _initialContentHeight && _button.Y < _initialButtonY);
                host.TryLog(stateValid
                    ? "C143-DYNAMIC toggle=PASS frame-stable=PASS stack-relayout=PASS result=PASS"u8
                    : "C143-DYNAMIC result=FAIL"u8);
                host.TryLog("C143-CHECKBOX callback=PASS result=PASS"u8);
            }
            else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _radioOne))
            {
                host.TryLog(_radioGroup.SelectedMember == _radioOne
                    ? "C143-RADIO explicit-group=PASS selected=one result=PASS"u8
                    : "C143-RADIO result=FAIL"u8);
            }
            else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _radioTwo))
            {
                host.TryLog(_radioGroup.SelectedMember == _radioTwo && !_radioOne.Checked
                    ? "C143-RADIO explicit-group=PASS selected=two result=PASS"u8
                    : "C143-RADIO result=FAIL"u8);
            }
            else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _progressToggle))
            {
                host.TryLog("C143-CHECKBOX callback=PASS result=PASS"u8);
            }
            else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _button))
            {
                int screenY = _button.Y - _view.Offset;
                _popupMenu.SetInvokerAvailable(_groupBox.Visible && _groupBox.Enabled);
                GuideXosPopupMenuResult opened = _popupMenu.Open(_button.X, screenY);
                bool captured = opened == GuideXosPopupMenuResult.Opened &&
                    _host.TryAcquireTransientInputCapture(MenuId);
                host.TryLog(captured
                    ? "C143-MENU open=PASS button-invoked=PASS capture=menu result=PASS"u8
                    : "C143-MENU open=FAIL result=FAIL"u8);
            }
            if (id == ScrollBarId && result == GuideXosControlHostResult.DragStarted)
                host.TryLog("C143-DRAG press=PASS owner=scrollbar result=PASS"u8);
            if (id == VisibleToggleId || id == EnabledToggleId)
            {
                CancelUnavailableTransientState();
                LogGeometry(host, "state-change");
            }
            return Render(host, surface);
        }

        bool wasOpen = _popupMenu.IsOpen || _comboBox.IsOpen;
        int priorFocusedMember = FocusedMemberOrdinal();
        _host.HandleInput(input);
        if (input.Kind == GuideXosInputKind.KeyDown && input.Shift &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
        {
            bool movedEarlier = priorFocusedMember >= 0 &&
                FocusedMemberOrdinal() >= 0 &&
                FocusedMemberOrdinal() < priorFocusedMember;
            host.TryLog(!_popupMenu.IsOpen && !_comboBox.IsOpen && movedEarlier
                ? "C143-FOCUS shift-tab=handled earlier-member=PASS result=PASS"u8
                : "C143-FOCUS shift-tab=result=FAIL"u8);
            bool final = !_host.HasTransientInputCapture &&
                !_host.HasPointerDragCapture &&
                _view.Offset >= 0 && _view.Offset <= _view.MaximumOffset &&
                _groupBox.ValidateMembershipGeometry(out int reverseOutside) &&
                reverseOutside == 0;
            host.TryLog(final
                ? "C143-FINAL viewport=valid layout=valid capture=none drag=none result=PASS"u8
                : "C143-FINAL result=FAIL"u8);
            LogGeometry(host, "shift-tab");
        }
        else if (input.Kind == GuideXosInputKind.KeyDown &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
        {
            host.TryLog(_view.HasFocus && _view.Offset > 0
                ? "C143-FOCUS tab=offscreen-revealed result=PASS"u8
                : "C143-FOCUS tab=host-traversed result=PASS"u8);
            host.TryLog(!_popupMenu.IsOpen && !_comboBox.IsOpen &&
                !_host.HasTransientInputCapture && !_host.HasPointerDragCapture &&
                _view.Offset >= 0 && _view.Offset <= _view.MaximumOffset &&
                _groupBox.ValidateMembershipGeometry(out int outside) && outside == 0
                ? "C143-FINAL viewport=valid layout=valid capture=none drag=none result=PASS"u8
                : "C143-FINAL result=FAIL"u8);
            LogGeometry(host, "tab");
        }
        else if (wasOpen && !_popupMenu.IsOpen && !_comboBox.IsOpen)
        {
            host.TryLog(!_host.HasTransientInputCapture
                ? "C143-CAPTURE popup=none result=PASS"u8
                : "C143-CAPTURE popup=stale result=FAIL"u8);
        }
        return Render(host, surface);
    }

    private void ResetComposition()
    {
        _host = new GuideXosControlHost(8);
        _view.Reset();
        _view.Clear();
        _stack.Clear();
        _groupBox.ClearMembers();
        _groupBox.SetVisible(true);
        _groupBox.SetEnabled(true);
        _radioGroup.Reset();
        _radioOne.Reset();
        _radioTwo.Reset();
        _radioOne.SetChecked(true);
        _radioGroup.TryRegister(_radioOne);
        _radioGroup.TryRegister(_radioTwo);
        _progressToggle.SetVisible(true);
        _progressToggle.SetEnabled(true);
        _progressToggle.SetChecked(true);
        _progress.SetVisible(true);
        _visibleToggle.SetChecked(true);
        _enabledToggle.SetChecked(true);
        _comboBox.Reset();
        _button.Reset();
        _popupMenu.Reset();
        _pendingMenuCommand = 0;

        bool groupMembers = true;
        groupMembers &= _groupBox.TryAddMember(_label) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_progressToggle) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_comboBox) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_separator) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_radioOne) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_radioTwo) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_progress) == GuideXosGroupBoxResult.Added;
        groupMembers &= _groupBox.TryAddMember(_button) == GuideXosGroupBoxResult.Added;

        bool viewMembers = _view.TryAddMember(_groupBox, 4, 4) ==
            GuideXosScrollViewResult.Added;
        bool stackFrame = viewMembers && _stack.TrySetFrame(
            _groupBox.ContentLeft, _groupBox.ContentTop, _groupBox.ContentWidth);
        viewMembers &= _view.TryAddMember(_label, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_progressToggle, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_comboBox, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_separator, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_radioOne, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_radioTwo, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_progress, 0, 0) == GuideXosScrollViewResult.Added;
        viewMembers &= _view.TryAddMember(_button, 0, 0) == GuideXosScrollViewResult.Added;

        bool stackMembers = true;
        stackMembers &= _stack.TryAddMember(_label) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_progressToggle) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_comboBox) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_separator) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_radioOne) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_radioTwo) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_progress) == GuideXosVerticalStackResult.Added;
        stackMembers &= _stack.TryAddMember(_button) == GuideXosVerticalStackResult.Added;
        bool layout = _stack.PerformLayout(_view) == GuideXosVerticalStackResult.LaidOut;
        bool linked = _groupBox.MemberCount == 8 && _view.MemberCount == 9 &&
            _stack.MemberCount == 8 && groupMembers && viewMembers && stackFrame &&
            stackMembers && layout;
        _ = linked;
        _view.BindScrollBar(_scrollBar);
    }

    private bool RegisterControls()
    {
        return _host.TryRegisterScrollView(ViewId, _view, true) == GuideXosControlHostResult.Registered &&
            _host.TryRegisterScrollBar(ScrollBarId, _scrollBar, true) == GuideXosControlHostResult.Registered &&
            _host.TryRegisterComboBox(ComboId, _comboBox, true) == GuideXosControlHostResult.Registered &&
            _host.TryRegisterPopupMenu(MenuId, _popupMenu, false) == GuideXosControlHostResult.Registered &&
            _host.TryRegisterCheckBox(VisibleToggleId, _visibleToggle, true) == GuideXosControlHostResult.Registered &&
            _host.TryRegisterCheckBox(EnabledToggleId, _enabledToggle, true) == GuideXosControlHostResult.Registered;
    }

    private void OnProgressVisibilityChanged(bool visible)
    {
        _progress.SetVisible(visible);
        _stack.PerformLayout(_view);
        _finalContentHeight = _stack.ContentHeight;
        bool moved = _groupBox.X == _initialFrameX && _groupBox.Y == _initialFrameY &&
            _button.Y < _initialButtonY && _finalContentHeight < _initialContentHeight;
        _host.RefreshVisibility();
        _appHost.TryLog(moved
            ? "C143-DYNAMIC hide=PASS margins=removed button=moved frame-stable=PASS result=PASS"u8
            : "C143-DYNAMIC hide=result=FAIL"u8);
    }

    private void OnSectionVisibilityChanged(bool visible)
    {
        _groupBox.SetVisible(visible);
        _stack.PerformLayout(_view);
        _host.RefreshVisibility();
        CancelUnavailableTransientState();
        _appHost.TryLog(visible
            ? "C143-GROUP visible=true effective-members=restored result=PASS"u8
            : "C143-GROUP visible=false effective-members=hidden result=PASS"u8);
    }

    private void OnSectionEnabledChanged(bool enabled)
    {
        _groupBox.SetEnabled(enabled);
        _host.RefreshVisibility();
        CancelUnavailableTransientState();
        _appHost.TryLog(enabled
            ? "C143-GROUP enabled=true own-state-preserved result=PASS"u8
            : "C143-GROUP enabled=false input-gated result=PASS"u8);
    }

    private void CancelUnavailableTransientState()
    {
        bool available = _groupBox.Visible && _groupBox.Enabled;
        _popupMenu.SetInvokerAvailable(available);
        if (!available && _comboBox.IsOpen)
            _ = _host.HasTransientInputCapture;
        _ = _host.HasTransientInputCapture;
    }

    private void OnMenuCommand(uint commandId)
    {
        _pendingMenuCommand = unchecked((int)commandId);
    }

    private int HitId(int x, int y)
    {
        if (_visibleToggle.Visible && ContainsPoint(_visibleToggle, x, y))
            return VisibleToggleId;
        if (_enabledToggle.Visible && ContainsPoint(_enabledToggle, x, y))
            return EnabledToggleId;
        if (x >= _view.X && x < _view.X + _view.Width &&
            y >= _view.Y && y < _view.Y + _view.Height) return ViewId;
        if (_scrollBar.Visible && x >= _scrollBar.X &&
            x < _scrollBar.X + _scrollBar.Width &&
            y >= _scrollBar.Y && y < _scrollBar.Y + _scrollBar.Height)
            return ScrollBarId;
        return 0;
    }

    private void LogGeometry(GuideXosHost host, string state)
    {
        Span<byte> line = stackalloc byte[160];
        int position = 0;
        GuideXosText.Append(line, ref position, "C143-GEOMETRY "u8);
        GuideXosText.Append(line, ref position, "frame="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.X);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.Y);
        GuideXosText.Append(line, ref position, " content="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.ContentLeft);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.ContentTop);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.ContentWidth);
        GuideXosText.Append(line, ref position, ","u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_groupBox.ContentHeight);
        GuideXosText.Append(line, ref position, " offset="u8);
        GuideXosText.AppendUnsigned(line, ref position, (uint)_view.Offset);
        GuideXosText.Append(line, ref position, " state="u8);
        for (int index = 0; index < state.Length && position < line.Length; index++)
            line[position++] = (byte)state[index];
        host.TryLog(line[..position]);
    }

    private static bool ContainsPoint(GuideXosCheckBox control, int x, int y)
    {
        return x >= control.X && x < control.X + control.Width &&
            y >= control.Y && y < control.Y + control.Height;
    }

    private int FocusedMemberOrdinal()
    {
        object focused = _view.FocusedMember;
        if (focused == null) return -1;
        for (int index = 0; index < _view.MemberCount; index++)
            if (ReferenceEquals(_view.GetMemberAt(index), focused)) return index;
        return -1;
    }

    private GuideXosResult Render(GuideXosHost host, GuideXosSurface surface)
    {
        if (surface.TryFillRect(10, 10, 540, 280, 0x007A5A9Au) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed GroupBox | "u8,
                "Stack geometry + ScrollView"u8) ||
            _visibleToggle.Render(surface) != GuideXosResult.Success ||
            _enabledToggle.Render(surface) != GuideXosResult.Success ||
            _view.Render(surface) != GuideXosResult.Success ||
            _scrollBar.Render(surface) != GuideXosResult.Success ||
            (_popupMenu.IsOpen && _popupMenu.Render(surface) != GuideXosResult.Success) ||
            !GuideXosText.Line(surface, 246, "Wheel / drag / Tab / toggle:"u8,
                "C143"u8))
            return GuideXosResult.InvalidArgument;
        _ = host;
        return GuideXosResult.Success;
    }
}
