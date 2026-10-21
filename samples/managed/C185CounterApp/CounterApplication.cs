using System.Runtime.CompilerServices;
using GuideXos.ManagedUi;

namespace GuideXos.C185.CounterApp;

public sealed class CounterApplicationFactory : IManagedUiApplicationFactory
{
    public const uint StableApplicationId = 0xC1850001u;

    public uint ApplicationId => StableApplicationId;

    [MethodImpl(MethodImplOptions.NoInlining)]
    public IManagedUiApplication Create() => new CounterApplication();
}
public sealed class CounterApplication : IManagedUiApplication
{
    public const uint IncrementCommandId = 1u;
    private const int DefaultOuterWidth = 360;
    private const int DefaultOuterHeight = 200;
    private const int MinimumOuterWidth = 320;
    private const int MinimumOuterHeight = 180;
    private const int MaximumOuterWidth = 720;
    private const int MaximumOuterHeight = 480;
    private const int Padding = 16;
    private const int ButtonWidth = 120;
    private const int ButtonHeight = 28;
    private const int LabelTop = 48;

    private IManagedUiWindow _window;
    private IManagedUiLabel _status;
    private IManagedUiButton _button;
    private ManagedUiApplicationIdentity _identity;
    private int _counter;
    private int _outerWidth = DefaultOuterWidth;
    private int _outerHeight = DefaultOuterHeight;
    private int _priorOuterWidth;
    private int _priorOuterHeight;
    private bool _resizePending;
    private bool _tornDown;

    [MethodImpl(MethodImplOptions.NoInlining)]
    public CounterApplication()
    {
    }

    public int Counter => _counter;
    public ManagedUiApplicationIdentity Identity => _identity;

    [MethodImpl(MethodImplOptions.NoInlining)]
    public ManagedUiResult Initialize(IManagedUiHostServices services,
        in ManagedUiApplicationIdentity identity)
    {
        if (services == null || identity.ApplicationId != CounterApplicationFactory.StableApplicationId ||
            identity.Generation == 0u || _window != null || _tornDown)
            return ManagedUiResult.InvalidArgument;

        _identity = identity;
        ManagedUiResult result = services.TryCreateWindow(
            "Managed Counter"u8, DefaultOuterWidth, DefaultOuterHeight,
            out IManagedUiWindow window);
        if (result != ManagedUiResult.Success || window == null)
            return result == ManagedUiResult.Success
                ? ManagedUiResult.WindowCreationFailed : result;

        _window = window;
        _outerWidth = DefaultOuterWidth;
        _outerHeight = DefaultOuterHeight;
        _counter = 0;
        result = window.TryCreateLabel(16, LabelTop, 328,
            "Activations: 0"u8, out _status);
        if (result != ManagedUiResult.Success) return result;
        result = window.TryCreateButton(IncrementCommandId, 0, 0,
            ButtonWidth, ButtonHeight, "Add one"u8, out _button);
        if (result != ManagedUiResult.Success) return result;
        if (!TryApplyLayout(DefaultOuterWidth, DefaultOuterHeight))
            return ManagedUiResult.InvalidArgument;
        return Render();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public ManagedUiResult Render()
    {
        if (_window == null || _status == null || _button == null || _tornDown)
            return ManagedUiResult.InvalidState;
        IManagedUiSurface surface = _window.Surface;
        ManagedUiResult result = surface.TryFillRect(8, 8,
            _window.OuterWidth - 16, _window.OuterHeight - 16, 0xFF202830u);
        if (result != ManagedUiResult.Success) return result;
        result = _status.Render();
        if (result != ManagedUiResult.Success) return result;
        return _button.Render();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public ManagedUiResult HandleAction(uint actionId)
    {
        if (actionId != IncrementCommandId) return ManagedUiResult.Unsupported;
        if (_window == null || _status == null || _tornDown)
            return ManagedUiResult.InvalidState;

        int next = _counter == int.MaxValue ? 0 : _counter + 1;
        Span<byte> text = stackalloc byte[32];
        int length = FormatCounter(text, next);
        ManagedUiResult result = _status.SetText(text[..length]);
        if (result != ManagedUiResult.Success) return result;
        int prior = _counter;
        _counter = next;
        result = _window.RequestRedraw();
        if (result == ManagedUiResult.Success) return result;

        _counter = prior;
        _ = FormatCounter(text, prior, out length);
        _ = _status.SetText(text[..length]);
        _ = _window.RequestRedraw();
        return result;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public ManagedUiResult HandleInput(in ManagedUiInputEvent input)
    {
        if (_window == null || _tornDown) return ManagedUiResult.InvalidState;
        ManagedUiResult result = _window.RouteInput(input,
            out uint activatedCommandId);
        if (result != ManagedUiResult.Success) return result;
        return activatedCommandId == 0u
            ? ManagedUiResult.Success : HandleAction(activatedCommandId);
    }

    public bool TryGetResizeBounds(out ManagedUiResizeBounds bounds)
    {
        bounds = new ManagedUiResizeBounds(MinimumOuterWidth,
            MinimumOuterHeight, MaximumOuterWidth, MaximumOuterHeight);
        return _window != null && !_tornDown;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public ManagedUiResizeOutcome HandleResize(
        in ManagedUiResizeProposal proposal)
    {
        if (_window == null || _status == null || _button == null ||
            _resizePending || proposal.PriorOuterWidth != _outerWidth ||
            proposal.PriorOuterHeight != _outerHeight)
        {
            return new(false, ManagedUiResult.InvalidState, false);
        }

        _priorOuterWidth = _outerWidth;
        _priorOuterHeight = _outerHeight;
        _resizePending = true;
        if (!TryApplyLayout(proposal.OuterWidth, proposal.OuterHeight))
            return new(false, ManagedUiResult.Success, false);

        ManagedUiResult render = Render();
        bool state = render == ManagedUiResult.Success &&
            _outerWidth == proposal.OuterWidth &&
            _outerHeight == proposal.OuterHeight &&
            _status.X == 16 && _button.X == proposal.ClientWidth - Padding - ButtonWidth &&
            _button.Y == proposal.ClientHeight - Padding - ButtonHeight;
        return new(true, render, state);
    }

    public void CompleteResize(bool accepted)
    {
        if (!_resizePending) return;
        if (!accepted)
        {
            _ = TryApplyLayout(_priorOuterWidth, _priorOuterHeight);
            _ = Render();
        }
        _resizePending = false;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void OnTearingDown()
    {
        if (_tornDown) return;
        _tornDown = true;
        _resizePending = false;
        _ = _window?.TryClose();
        _window = null;
        _status = null;
        _button = null;
        _counter = 0;
        _outerWidth = DefaultOuterWidth;
        _outerHeight = DefaultOuterHeight;
    }

    private bool TryApplyLayout(int outerWidth, int outerHeight)
    {
        if (_window == null || _status == null || _button == null ||
            outerWidth < MinimumOuterWidth || outerHeight < MinimumOuterHeight)
            return false;

        int clientHeight = outerHeight - 24;
        int buttonX = outerWidth - Padding - ButtonWidth;
        int buttonY = clientHeight - Padding - ButtonHeight;
        int labelWidth = Math.Min(outerWidth - 32, 512);
        labelWidth -= labelWidth % 8;
        if (buttonX < Padding || buttonY < LabelTop + 18 ||
            _status.TrySetBounds(16, LabelTop, labelWidth) != ManagedUiResult.Success ||
            _button.TrySetBounds(buttonX, buttonY, ButtonWidth, ButtonHeight) !=
                ManagedUiResult.Success)
            return false;

        _outerWidth = outerWidth;
        _outerHeight = outerHeight;
        return true;
    }

    private static int FormatCounter(Span<byte> destination, int value) =>
        FormatCounter(destination, value, out int length) ? length : 0;

    private static bool FormatCounter(Span<byte> destination, int value,
        out int length)
    {
        ReadOnlySpan<byte> prefix = "Activations: "u8;
        if (destination.Length < prefix.Length)
        {
            length = 0;
            return false;
        }
        prefix.CopyTo(destination);
        length = prefix.Length;
        uint remaining = (uint)value;
        Span<byte> digits = stackalloc byte[10];
        int count = 0;
        do
        {
            digits[count++] = (byte)('0' + remaining % 10u);
            remaining /= 10u;
        } while (remaining != 0u);
        if (count > destination.Length - length) return false;
        while (count > 0) destination[length++] = digits[--count];
        return true;
    }
}
