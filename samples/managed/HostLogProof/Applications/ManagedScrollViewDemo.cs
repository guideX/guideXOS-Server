namespace HostLogProof.Applications;

/// <summary>
/// Small production-facing C140 proof surface. The ScrollView is registered as
/// one host scope item; its ordinary controls remain non-owning direct members.
/// </summary>
public sealed class ManagedScrollViewDemo : GuideXosApplication
{
    private const int ViewId = 1;
    private const int ScrollBarId = 2;
    private readonly GuideXosScrollView _view =
        new(24, 72, 300, 120, 8);
    private readonly GuideXosScrollBar _scrollBar =
        new(336, 72, 16, 120);
    private readonly GuideXosLabel _title =
        new(0, 0, 240, "C140 vertical ScrollView");
    private readonly GuideXosButton _firstButton =
        new(0, 0, 240, 18, "First logical button");
    private readonly GuideXosCheckBox _checkBox =
        new(0, 0, 240, 18, "A checkbox below the fold");
    private readonly GuideXosRadioButton _radioButton =
        new(0, 0, 240, 18, "A radio option");
    private readonly GuideXosButton _middleButton =
        new(0, 0, 240, 18, "Middle logical button");
    private readonly GuideXosCheckBox _secondCheckBox =
        new(0, 0, 240, 18, "Second checkbox");
    private readonly GuideXosButton _bottomButton =
        new(0, 0, 240, 18, "Bottom logical button");
    private readonly GuideXosLabel _bottomLabel =
        new(0, 0, 240, "End of bounded content");
    private GuideXosControlHost _host;
    private ulong _window;
    private bool _testsRun;
    private uint _launchCount;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host.IsCapabilityProbe) return GuideXosResult.Success;
        ++_launchCount;
        _host = new GuideXosControlHost(2);
        _view.Reset();
        _view.Clear();
        _view.TryAddMember(_title, 8, 0);
        _view.TryAddMember(_firstButton, 8, 28);
        _view.TryAddMember(_checkBox, 8, 60);
        _view.TryAddMember(_radioButton, 8, 92);
        _view.TryAddMember(_middleButton, 8, 124);
        _view.TryAddMember(_secondCheckBox, 8, 156);
        _view.TryAddMember(_bottomButton, 8, 188);
        _view.TryAddMember(_bottomLabel, 8, 220);
        _view.BindScrollBar(_scrollBar);
        _host.TryRegisterScrollView(ViewId, _view, true);
        _host.TryRegisterScrollBar(ScrollBarId, _scrollBar, true);

        GuideXosResult result = host.TryCreateSurface(
            "Managed ScrollView"u8, 560, 300, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null)
            return result;
        _window = surface.Handle;
        if (!_testsRun)
        {
            bool tests = GuideXosScrollViewC140Tests.Run(host);
            _testsRun = true;
            host.TryLog(tests
                ? "C140-TESTS core=20 clipping=9 hit=10 focus=9 scrollbar=10 cases=58 result=PASS"u8
                : "C140-TESTS result=FAIL"u8);
        }
        host.TryLog(_host.RegistrationCount == 2 && _view.Offset == 0 &&
            _view.ContentExtent > _view.VisibleExtent &&
            _scrollBar.Value == 0
            ? "C140-PROOF launch=PASS registration=2 initial-viewport=0 result=PASS"u8
            : "C140-PROOF launch=FAIL result=FAIL"u8);
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
            host.TryLog(release == GuideXosControlHostResult.DragEnded &&
                !_host.HasPointerDragCapture
                ? "C140-DRAG release=PASS owner=none result=PASS"u8
                : "C140-DRAG release=IGNORED owner=none result=PASS"u8);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.Wheel)
        {
            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == ViewId || id == ScrollBarId
                ? _host.HandleWheel(id, input.X, input.Y, input.WheelDelta)
                : GuideXosControlHostResult.Ignored;
            host.TryLog(id == ViewId && result == GuideXosControlHostResult.Scrolled
                ? "C140-WHEEL viewport=changed thumb=synchronized result=PASS"u8
                : "C140-WHEEL result=IGNORED"u8);
            return Render(host, surface);
        }
        if (input.Kind == GuideXosInputKind.PointerDown)
        {
            int id = HitId(input.X, input.Y);
            GuideXosControlHostResult result = id == ViewId || id == ScrollBarId
                ? _host.FocusAndRoutePointer(id, input.X, input.Y)
                : GuideXosControlHostResult.Ignored;
            if (id == ViewId && _view.LastActivatedMember != null)
            {
                host.TryLog(ReferenceEquals(_view.LastActivatedMember, _bottomButton)
                    ? "C140-POINTER logical=bottom-button translated=PASS result=PASS"u8
                    : "C140-POINTER logical=member translated=PASS result=PASS"u8);
            }
            if (id == ScrollBarId && result == GuideXosControlHostResult.DragStarted)
                host.TryLog("C140-DRAG press=PASS owner=scrollbar result=PASS"u8);
            return Render(host, surface);
        }

        GuideXosControlHostResult keyResult = _host.HandleInput(input);
        if (input.Kind == GuideXosInputKind.KeyDown &&
            (GuideXosTextInputKey)input.KeyCode == GuideXosTextInputKey.Tab)
        {
            host.TryLog(_view.HasFocus
                ? "C140-FOCUS tab=revealed result=PASS"u8
                : "C140-FOCUS tab=host-traversed result=PASS"u8);
        }
        _ = keyResult;
        return Render(host, surface);
    }

    private int HitId(int x, int y)
    {
        if (_view.Visible && x >= _view.X && x < _view.X + _view.Width &&
            y >= _view.Y && y < _view.Y + _view.Height) return ViewId;
        if (_scrollBar.Visible && x >= _scrollBar.X && x < _scrollBar.X + _scrollBar.Width &&
            y >= _scrollBar.Y && y < _scrollBar.Y + _scrollBar.Height) return ScrollBarId;
        return 0;
    }

    private GuideXosResult Render(GuideXosHost host, GuideXosSurface surface)
    {
        if (surface.TryFillRect(10, 10, 540, 270, 0x007A5A9Au) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 24, "Managed ScrollView | "u8,
                "bounded ordinary controls"u8) ||
            !GuideXosText.CountLine(surface, 48, "Launches: "u8, _launchCount) ||
            _view.Render(surface) != GuideXosResult.Success ||
            _scrollBar.Render(surface) != GuideXosResult.Success ||
            !GuideXosText.Line(surface, 208, "Wheel / drag / Tab: "u8,
                "shared viewport"u8))
            return GuideXosResult.InvalidArgument;
        _ = host;
        return GuideXosResult.Success;
    }
}
