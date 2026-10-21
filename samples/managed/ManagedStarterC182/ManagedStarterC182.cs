using System;
using HostLogProof;

namespace HostLogProof.Applications;

/// <summary>
/// Non-registered starter showing instance-owned state, existing controls,
/// input routing, and the C180 resize opt-in contract.
/// </summary>
public sealed class ManagedStarterC182 : GuideXosApplication
{
    internal const uint IncrementActionId = 1u;
    internal const int DefaultOuterWidth = 360;
    internal const int DefaultOuterHeight = 200;
    internal const int MinimumOuterWidth = 320;
    internal const int MinimumOuterHeight = 180;
    internal const int MaximumOuterWidth = 720;
    internal const int MaximumOuterHeight = 480;
    private const int IncrementButtonId = 1;
    private const int IncrementButtonWidth = 120;
    private const int IncrementButtonHeight = 28;
    private const int Padding = 16;
    private const int ContentTop = 48;

    private GuideXosSurface _surface;
    private GuideXosLabel _status;
    private GuideXosButton _incrementButton;
    private GuideXosControlHost _controls;
    private int _activationCount;
    private int _outerWidth = DefaultOuterWidth;
    private int _outerHeight = DefaultOuterHeight;

    internal int ActivationCount => _activationCount;
    internal int OuterWidth => _outerWidth;
    internal int OuterHeight => _outerHeight;
    internal int StatusX => _status?.X ?? -1;
    internal int StatusWidth => _status?.Width ?? 0;
    internal int ButtonX => _incrementButton?.X ?? -1;
    internal int ButtonY => _incrementButton?.Y ?? -1;
    internal string StatusText => _status?.Text ?? string.Empty;
    internal ulong WindowHandle => _surface?.Handle ?? 0u;
    internal bool HasSurface => _surface != null;
    internal int RegisteredControlCount => _controls?.RegistrationCount ?? 0;

    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host == null) return GuideXosResult.InvalidArgument;
        if (_surface != null) return Render();

        GuideXosResult result = host.TryCreateSurface(
            "Managed Starter"u8, DefaultOuterWidth, DefaultOuterHeight,
            out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null) return result;

        _surface = surface;
        _outerWidth = DefaultOuterWidth;
        _outerHeight = DefaultOuterHeight;
        _activationCount = 0;
        _status = new GuideXosLabel(16, ContentTop, 328,
            "Activations: 0", maximumTextLength: 48);
        _incrementButton = new GuideXosButton(0, 0,
            IncrementButtonWidth, IncrementButtonHeight, "Add one");
        _controls = new GuideXosControlHost(1);

        if (_controls.TryRegisterButton(IncrementButtonId, _incrementButton) !=
                GuideXosControlHostResult.Registered ||
            !TryApplyLayout(DefaultOuterWidth, DefaultOuterHeight))
        {
            return FailLaunch(GuideXosResult.InvalidArgument);
        }

        result = Render();
        return result == GuideXosResult.Success
            ? result
            : FailLaunch(result);
    }

    public override GuideXosResult HandleAction(GuideXosHost host, uint actionId)
    {
        if (actionId != IncrementActionId) return GuideXosResult.InvalidAction;
        if (_surface == null || _status == null || _incrementButton == null)
            return GuideXosResult.SurfaceCreationFailed;

        int nextCount = _activationCount == int.MaxValue
            ? 0 : _activationCount + 1;
        if (!_status.SetText(FormatStatus(nextCount)))
            return GuideXosResult.InvalidArgument;

        int priorCount = _activationCount;
        _activationCount = nextCount;
        GuideXosResult result = Render();
        if (result == GuideXosResult.Success) return result;

        _activationCount = priorCount;
        _ = _status.SetText(FormatStatus(priorCount));
        _ = Render();
        return result;
    }

    public override GuideXosResult HandleInput(
        GuideXosHost host, GuideXosInputEvent input)
    {
        if (_surface == null || _controls == null || _incrementButton == null)
            return GuideXosResult.SurfaceCreationFailed;

        GuideXosControlHostResult routed = GuideXosControlHostResult.Ignored;
        if (input.Kind == GuideXosInputKind.PointerDown &&
            input.Button == GuideXosPointerButton.Primary)
        {
            if (input.X < _incrementButton.X || input.Y < _incrementButton.Y ||
                input.X >= _incrementButton.X + _incrementButton.Width ||
                input.Y >= _incrementButton.Y + _incrementButton.Height)
            {
                return GuideXosResult.Success;
            }
            routed = _controls.FocusAndRoutePointer(IncrementButtonId,
                input.X, input.Y);
        }
        else if (input.Kind == GuideXosInputKind.KeyDown ||
            input.Kind == GuideXosInputKind.KeyChar)
        {
            routed = _controls.HandleInput(input);
        }
        else
        {
            return GuideXosResult.Success;
        }

        if (routed == GuideXosControlHostResult.Activated)
            return HandleAction(host, IncrementActionId);
        if (routed == GuideXosControlHostResult.Traversed)
            return Render();
        return GuideXosResult.Success;
    }

    internal override bool TryGetManagedResizeGeometryC180(
        out GuideXosManagedWindowResizeBoundsC174 bounds,
        out int outerWidth, out int outerHeight)
    {
        bounds = new GuideXosManagedWindowResizeBoundsC174(
            MinimumOuterWidth, MinimumOuterHeight,
            MaximumOuterWidth, MaximumOuterHeight);
        outerWidth = _outerWidth;
        outerHeight = _outerHeight;
        return _surface != null;
    }

    internal override GuideXosResult HandleManagedResizeC180(
        GuideXosHost host,
        ref GuideXosManagedWindowResizeTransactionC174 transaction)
    {
        if (host == null || _surface == null || _status == null ||
            _incrementButton == null || !transaction.IsValid)
        {
            return GuideXosResult.SurfaceCreationFailed;
        }

        int priorWidth = _outerWidth;
        int priorHeight = _outerHeight;
        GuideXosManagedWindowResizeCandidateC174 candidate = transaction.Candidate;
        bool layoutAccepted = TryApplyLayout(
            candidate.OuterWidth, candidate.OuterHeight);
        GuideXosResult renderResult = layoutAccepted
            ? Render()
            : GuideXosResult.InvalidArgument;
        bool stateAccepted = layoutAccepted &&
            _outerWidth == candidate.OuterWidth &&
            _outerHeight == candidate.OuterHeight &&
            _status.X == 16 && _status.Width == Math.Min(
                candidate.ClientWidth - 32, 512) -
                Math.Min(candidate.ClientWidth - 32, 512) %
                    GuideXosLabel.CharacterWidth &&
            _incrementButton.X == candidate.ClientWidth - Padding -
                IncrementButtonWidth &&
            _incrementButton.Y == candidate.ClientHeight - Padding -
                IncrementButtonHeight;

        if (transaction.Complete(layoutAccepted, renderResult, stateAccepted))
            return GuideXosResult.Success;

        _ = TryApplyLayout(priorWidth, priorHeight);
        _ = Render();
        return transaction.Result;
    }

    public override void OnTearingDown()
    {
        _controls?.Reset();
        _incrementButton?.Reset();
        _status?.Reset();
        _controls = null;
        _incrementButton = null;
        _status = null;
        _surface = null;
        _activationCount = 0;
        _outerWidth = DefaultOuterWidth;
        _outerHeight = DefaultOuterHeight;
    }

    private GuideXosResult FailLaunch(GuideXosResult failure)
    {
        if (_surface != null) _ = _surface.TryClose();
        OnTearingDown();
        return failure;
    }

    private bool TryApplyLayout(int outerWidth, int outerHeight)
    {
        if (_status == null || _incrementButton == null ||
            outerWidth < MinimumOuterWidth ||
            outerHeight < MinimumOuterHeight)
        {
            return false;
        }

        int clientHeight = outerHeight -
            GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight;
        int statusWidth = Math.Min(outerWidth - 32, 512);
        statusWidth -= statusWidth % GuideXosLabel.CharacterWidth;
        int buttonX = outerWidth - Padding - IncrementButtonWidth;
        int buttonY = clientHeight - Padding - IncrementButtonHeight;
        if (buttonX < Padding || buttonY < ContentTop + GuideXosLabel.TextRowHeight)
            return false;

        if (!_status.TrySetBounds(16, ContentTop, statusWidth) ||
            !_incrementButton.TrySetBounds(buttonX, buttonY,
                IncrementButtonWidth, IncrementButtonHeight))
        {
            return false;
        }

        _outerWidth = outerWidth;
        _outerHeight = outerHeight;
        return true;
    }

    private GuideXosResult Render()
    {
        if (_surface == null || _status == null || _incrementButton == null)
            return GuideXosResult.SurfaceCreationFailed;

        int clientHeight = _outerHeight -
            GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight;
        if (_surface.TryFillRect(8, 8, _outerWidth - 16,
                clientHeight - 16, 0x00212D3Au) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        GuideXosResult statusResult = _status.Render(_surface);
        if (statusResult != GuideXosResult.Success) return statusResult;
        return _incrementButton.Render(_surface);
    }

    private static string FormatStatus(int count) => "Activations: " + count;
}
