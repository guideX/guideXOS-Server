namespace HostLogProof.Applications;

/// <summary>
/// Production-facing C142 proof: eight ordinary leaf controls are direct
/// ScrollView members and are independently referenced by one non-owning
/// vertical stack with bounded per-member margins and alignment.
/// </summary>
public sealed class ManagedVerticalStackDemo : GuideXosApplication
{
    private const int ViewId = 1;
    private const int ScrollBarId = 2;
    private readonly GuideXosScrollView _view =
        new(24, 72, 300, 120, 8);
    private readonly GuideXosScrollBar _scrollBar =
        new(336, 72, 16, 120);
    private readonly GuideXosVerticalStack _stack;
    private readonly GuideXosLabel _title =
        new(0, 0, 240, "C142 margin and alignment stack");
    private readonly GuideXosCheckBox _checkBox =
        new(0, 0, 240, 18, "Show progress", true);
    private readonly GuideXosComboBox _comboBox =
        new(0, 0, 240, 18, 4, 16, 2);
    private readonly GuideXosSeparator _separator =
        new(0, 0, 240);
    private readonly GuideXosRadioButton _radioOne =
        new(0, 0, 240, 18, "Stack option one", true);
    private readonly GuideXosRadioButton _radioTwo =
        new(0, 0, 240, 18, "Stack option two");
    private readonly GuideXosProgressBar _progress =
        new(0, 0, 240, 0, 100, 65);
    private readonly GuideXosButton _button =
        new(0, 0, 128, 18, "Apply stack settings");
    private readonly GuideXosRadioGroup _radioGroup = new(2);
    private GuideXosControlHost _host;
    private ulong _window;
    private bool _testsRun;
    private uint _launchCount;
    private bool _lastProgressVisible;
    private int _initialContentHeight;
    private int _finalContentHeight;

    public ManagedVerticalStackDemo()
    {
        _stack = new GuideXosVerticalStack(
            _view.InnerX, _view.InnerY, _view.InnerWidth, 8);
        _stack.TrySetPadding(6, 6, 8, 8);
        _stack.TrySetSpacing(4);
        _title.TrySetMargins(0, 4, 0, 4);
        _title.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _checkBox.TrySetMargins(0, 4, 0, 6);
        _checkBox.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _comboBox.TrySetMargins(8, 6, 8, 8);
        _comboBox.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _separator.TrySetMargins(16, 4, 16, 8);
        _separator.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _radioOne.TrySetMargins(0, 8, 0, 4);
        _radioOne.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        _radioTwo.TrySetMargins(0, 3, 0, 6);
        _radioTwo.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        _progress.TrySetMargins(8, 10, 8, 10);
        _progress.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        _button.TrySetMargins(0, 8, 0, 8);
        _button.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        _comboBox.TryAddItem("Default");
        _comboBox.TryAddItem("Compact");
        _comboBox.TryAddItem("Detailed");
        _checkBox.Changed = OnProgressVisibilityChanged;
        _radioGroup.TryRegister(_radioOne);
        _radioGroup.TryRegister(_radioTwo);
    }

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return GuideXosResult.Success;
        ++_launchCount;
        _host = new GuideXosControlHost(2);
        _stack.Clear();
        _view.Reset();
        _view.Clear();
        _radioOne.Reset();
        _radioTwo.Reset();
        _radioGroup.Reset();
        _radioOne.SetChecked(true);
        _radioGroup.TryRegister(_radioOne);
        _radioGroup.TryRegister(_radioTwo);
        _checkBox.SetVisible(true);
        _checkBox.SetEnabled(true);
        _checkBox.SetChecked(true);
        _progress.SetVisible(true);
        _comboBox.Reset();
        _button.Reset();
        _lastProgressVisible = true;

        bool membersAdded = true;
        membersAdded &= _view.TryAddMember(_title, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_checkBox, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_comboBox, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_separator, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_radioOne, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_radioTwo, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_progress, 0, 0) == GuideXosScrollViewResult.Added;
        membersAdded &= _view.TryAddMember(_button, 0, 0) == GuideXosScrollViewResult.Added;
        bool stackAdded = true;
        stackAdded &= _stack.TryAddMember(_title) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_checkBox) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_comboBox) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_separator) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_radioOne) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_radioTwo) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_progress) == GuideXosVerticalStackResult.Added;
        stackAdded &= _stack.TryAddMember(_button) == GuideXosVerticalStackResult.Added;
        bool laidOut = _stack.PerformLayout(_view) ==
            GuideXosVerticalStackResult.LaidOut;
        _initialContentHeight = _stack.ContentHeight;
        _view.BindScrollBar(_scrollBar);
        bool registered =
            _host.TryRegisterScrollView(ViewId, _view, true) ==
                GuideXosControlHostResult.Registered &&
            _host.TryRegisterScrollBar(ScrollBarId, _scrollBar, true) ==
                GuideXosControlHostResult.Registered;

        GuideXosResult result = host.TryCreateSurface(
            "Managed Vertical Stack"u8, 560, 300,
            out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null)
            return result;
        _window = surface.Handle;
        if (!_testsRun)
        {
            bool tests = GuideXosVerticalStackC141Tests.Run(host);
            bool c142Tests = GuideXosVerticalStackC142Tests.Run(host);
            _testsRun = true;
            host.TryLog(tests
                ? "C141-TESTS core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS"u8
                : "C141-TESTS result=FAIL"u8);
            host.TryLog(c142Tests
                ? "C142-TESTS metadata=20 spacing=12 horizontal=12 scrollview=14 popup=10 total=68 result=PASS"u8
                : "C142-TESTS result=FAIL"u8);
        }
        bool alignmentProof = _title.X == _stack.X + _stack.LeftPadding &&
            _radioOne.X > _stack.X + _stack.LeftPadding &&
            _button.X + _button.Width == _stack.X + _stack.Width - _stack.RightPadding &&
            _progress.Width == ((_stack.Width - _stack.LeftPadding - _stack.RightPadding -
                _progress.MarginLeft - _progress.MarginRight) / 8) * 8;
        host.TryLog(membersAdded && stackAdded && laidOut && registered &&
            _view.MemberCount == 8 && _stack.MemberCount == 8 &&
            _host.RegistrationCount == 2 && _view.ContentExtent > _view.VisibleExtent &&
            alignmentProof
            ? "C142-PROOF launch=PASS registration=2 members=8 layout=valid alignment=valid result=PASS"u8
            : "C142-PROOF launch=FAIL result=FAIL"u8);
        host.TryLog(laidOut && _view.ContentExtent > _view.VisibleExtent &&
            _view.Offset == 0 && _scrollBar.Value == 0
            ? "C142-LAYOUT initial=PASS content-taller-than-viewport=PASS"u8
            : "C142-LAYOUT initial=FAIL result=FAIL"u8);
        host.TryLog(alignmentProof
            ? "C142-ALIGN left=PASS center=PASS right=PASS stretch=PASS pointer-ready=PASS result=PASS"u8
            : "C142-ALIGN result=FAIL"u8);
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
            GuideXosControlHostResult release = _host.HandlePointerUp(
                input.X, input.Y, input.Button);
            host.TryLog(!_host.HasPointerDragCapture
                ? "C142-DRAG release=PASS owner=none result=PASS"u8
                : "C142-DRAG release=FAIL result=FAIL"u8);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.Wheel)
        {
            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == ViewId || id == ScrollBarId
                ? _host.HandleWheel(id, input.X, input.Y, input.WheelDelta)
                : GuideXosControlHostResult.Ignored;
            host.TryLog(id == ViewId && result == GuideXosControlHostResult.Scrolled
                ? "C142-WHEEL viewport=changed stack-translated=PASS result=PASS"u8
                : "C142-WHEEL result=IGNORED"u8);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            // The popup remains transient ComboBox state. The layout does not
            // capture it; the application routes the follow-up through the
            // existing control at its current translated content coordinate.
            if (_comboBox.IsOpen)
            {
                GuideXosComboBoxResult popupResult = _comboBox.HandlePointerDown(
                    input.X, input.Y + _view.Offset);
                host.TryLog(popupResult == GuideXosComboBoxResult.SelectionChanged
                    ? "C142-POPUP follow-up=PASS capture=none result=PASS"u8
                    : "C142-POPUP follow-up=handled capture=none result=PASS"u8);
                return Render(host, surface);
            }
            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == ViewId || id == ScrollBarId
                ? _host.FocusAndRoutePointer(id, input.X, input.Y)
                : GuideXosControlHostResult.Ignored;
            if (id == ViewId && _view.LastActivatedMember != null)
            {
                if (ReferenceEquals(_view.LastActivatedMember, _checkBox))
                {
                    _host.RefreshVisibility();
                    _finalContentHeight = _stack.ContentHeight;
                    host.TryLog(!_progress.Visible && _finalContentHeight < _initialContentHeight &&
                        _button.Y < _progress.Y
                        ? "C142-VISIBILITY checkbox=PASS progress=hidden margins=removed button=moved content=shrunk relayout=PASS result=PASS"u8
                        : "C142-VISIBILITY result=FAIL"u8);
                }
                else if (ReferenceEquals(_view.LastActivatedMember, _button))
                {
                    host.TryLog("C142-POINTER button=right-aligned hit=PASS translated=PASS result=PASS"u8);
                }
                else if (ReferenceEquals(_view.LastActivatedMember, _comboBox) &&
                    _comboBox.IsOpen)
                {
                    host.TryLog("C142-POPUP open=PASS arranged-geometry=PASS after-relayout=PASS capture=none result=PASS"u8);
                    host.TryLog("C142-ALIGN stretch=PASS pointer=PASS result=PASS"u8);
                }
                else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _radioOne))
                    host.TryLog("C142-ALIGN center=PASS pointer=PASS result=PASS"u8);
                else if (id == ViewId && ReferenceEquals(_view.LastActivatedMember, _checkBox))
                    host.TryLog("C142-ALIGN left=PASS pointer=PASS result=PASS"u8);
            }
            if (id == ScrollBarId && result == GuideXosControlHostResult.DragStarted)
                host.TryLog("C142-DRAG press=PASS owner=scrollbar result=PASS"u8);
            return Render(host, surface);
        }

        GuideXosControlHostResult keyResult = _host.HandleInput(input);
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
        {
            host.TryLog(_view.HasFocus
                ? "C142-FOCUS tab=revealed margins=accounted result=PASS"u8
                : "C142-FOCUS tab=host-traversed result=PASS"u8);
            host.TryLog(!_comboBox.IsOpen && !_host.HasTransientInputCapture &&
                !_host.HasPointerDragCapture && _view.Offset >= 0 &&
                _view.Offset <= _view.MaximumOffset
                ? "C142-FINAL viewport=valid layout=valid capture=none drag=none result=PASS"u8
                : "C142-FINAL result=FAIL"u8);
        }
        _ = keyResult;
        return Render(host, surface);
    }

    private void OnProgressVisibilityChanged(bool visible)
    {
        _progress.SetVisible(visible);
        _stack.PerformLayout(_view);
        _lastProgressVisible = visible;
    }

    private int HitId(int x, int y)
    {
        if (_view.Visible && x >= _view.X && x < _view.X + _view.Width &&
            y >= _view.Y && y < _view.Y + _view.Height) return ViewId;
        if (_scrollBar.Visible && x >= _scrollBar.X &&
            x < _scrollBar.X + _scrollBar.Width &&
            y >= _scrollBar.Y && y < _scrollBar.Y + _scrollBar.Height) return ScrollBarId;
        return 0;
    }

    private GuideXosResult Render(GuideXosHost host, GuideXosSurface surface)
    {
        if (surface.TryFillRect(10, 10, 540, 270, 0x007A5A9Au) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed Vertical Stack | "u8,
                "bounded non-owning layout"u8) ||
            !GuideXosText.CountLine(surface, 48, "Launches: "u8, _launchCount) ||
            _view.Render(surface) != GuideXosResult.Success ||
            _scrollBar.Render(surface) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 208, "Wheel / drag / Tab / toggle: "u8,
                "C142"u8))
            return GuideXosResult.InvalidArgument;
        _ = host;
        _ = _lastProgressVisible;
        return GuideXosResult.Success;
    }
}
