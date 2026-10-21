#if HOSTLOGPROOF_C185_BOUNDARY
using System;
using System.Runtime.CompilerServices;
using System.Text;
using GuideXos.C185.CounterApp;
using GuideXos.ManagedUi;

namespace HostLogProof;

internal sealed class GuideXosC185ApplicationBridge : GuideXosApplication
{
    private IManagedUiApplication _application;
    private GuideXosC185HostServicesAdapter _services;
    private ManagedUiApplicationIdentity _identity;
    private bool _initialized;
    private bool _teardownComplete;

    internal GuideXosC185ApplicationBridge(IManagedUiApplication application)
    {
        _application = application;
    }

    internal IManagedUiApplication ApplicationForTests => _application;
    internal GuideXosC185HostServicesAdapter ServicesForTests => _services;

    internal GuideXosResult RenderForWindow()
    {
        if (!_initialized || _application == null || _teardownComplete)
            return GuideXosResult.InvalidArgument;
        return GuideXosC185ResultMap.ToHost(_application.Render());
    }

    internal void SetIdentity(uint applicationId, uint generation)
    {
        _identity = new ManagedUiApplicationIdentity(applicationId, generation);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public override GuideXosResult Launch(GuideXosHost host)
    {
        if (host == null || _application == null || _teardownComplete ||
            _identity.Generation == 0u || _initialized)
            return GuideXosResult.InvalidArgument;

        _services = new GuideXosC185HostServicesAdapter(host, this, _identity);
        ManagedUiResult result = _application.Initialize(_services,
            in _identity);
        if (result != ManagedUiResult.Success)
        {
            ReleaseAfterInitializationFailure();
            return GuideXosC185ResultMap.ToHost(result);
        }

        _initialized = true;
        return GuideXosResult.Success;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public override GuideXosResult HandleAction(GuideXosHost host,
        uint actionId)
    {
        _ = host;
        if (!_initialized || _application == null || _teardownComplete)
            return GuideXosResult.InvalidArgument;
        return GuideXosC185ResultMap.ToHost(_application.HandleAction(actionId));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public override GuideXosResult HandleInput(GuideXosHost host,
        GuideXosInputEvent input)
    {
        _ = host;
        if (!_initialized || _application == null || _teardownComplete)
            return GuideXosResult.InvalidArgument;
        ManagedUiInputEvent externalInput = GuideXosC185InputMap.From(input);
        return GuideXosC185ResultMap.ToHost(
            _application.HandleInput(in externalInput));
    }

    internal override bool TryGetManagedResizeGeometryC180(
        out GuideXosManagedWindowResizeBoundsC174 bounds,
        out int outerWidth, out int outerHeight)
    {
        bounds = default;
        outerWidth = 0;
        outerHeight = 0;
        if (!_initialized || _application == null || _services?.Window == null ||
            !_application.TryGetResizeBounds(out ManagedUiResizeBounds externalBounds) ||
            !externalBounds.IsValid)
            return false;

        bounds = new GuideXosManagedWindowResizeBoundsC174(
            externalBounds.MinimumOuterWidth, externalBounds.MinimumOuterHeight,
            externalBounds.MaximumOuterWidth, externalBounds.MaximumOuterHeight);
        outerWidth = _services.Window.OuterWidth;
        outerHeight = _services.Window.OuterHeight;
        return true;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal override GuideXosResult HandleManagedResizeC180(
        GuideXosHost host,
        ref GuideXosManagedWindowResizeTransactionC174 transaction)
    {
        _ = host;
        GuideXosC185WindowAdapter window = _services?.Window;
        if (!_initialized || _application == null || window == null ||
            !transaction.IsValid)
            return GuideXosResult.InvalidArgument;

        GuideXosManagedWindowResizeCandidateC174 candidate = transaction.Candidate;
        ManagedUiResizeProposal proposal = new(
            transaction.PriorOuterWidth, transaction.PriorOuterHeight,
            candidate.OuterWidth, candidate.OuterHeight,
            candidate.ClientWidth, candidate.ClientHeight);
        window.SetGeometry(candidate.OuterWidth, candidate.OuterHeight);
        ManagedUiResizeOutcome outcome = _application.HandleResize(in proposal);
        bool accepted = transaction.Complete(outcome.LayoutAccepted,
            GuideXosC185ResultMap.ToHost(outcome.RenderResult),
            outcome.StateAccepted);

        if (!accepted)
            window.SetGeometry(transaction.PriorOuterWidth,
                transaction.PriorOuterHeight);
        _application.CompleteResize(accepted);
        return transaction.Result;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public override void OnTearingDown()
    {
        if (_teardownComplete) return;
        _teardownComplete = true;
        _initialized = false;
        _application?.OnTearingDown();
        _services?.Release();
        _services = null;
        _application = null;
    }

    internal void SetResizeGeometryForTests(int width, int height)
    {
        _services?.Window?.SetGeometry(width, height);
    }

    private void ReleaseAfterInitializationFailure()
    {
        if (_teardownComplete) return;
        _teardownComplete = true;
        _application?.OnTearingDown();
        _services?.Release();
        _services = null;
        _application = null;
    }
}

internal sealed class GuideXosC185HostServicesAdapter : IManagedUiHostServices
{
    private readonly GuideXosHost _host;
    private readonly GuideXosC185ApplicationBridge _owner;
    private readonly ManagedUiApplicationIdentity _identity;
    private bool _released;

    internal GuideXosC185HostServicesAdapter(GuideXosHost host,
        GuideXosC185ApplicationBridge owner,
        ManagedUiApplicationIdentity identity)
    {
        _host = host;
        _owner = owner;
        _identity = identity;
    }

    internal GuideXosC185WindowAdapter Window { get; private set; }

    public ManagedUiResult TryCreateWindow(ReadOnlySpan<byte> title,
        int outerWidth, int outerHeight, out IManagedUiWindow window)
    {
        window = null;
        if (_released || _host == null) return ManagedUiResult.InvalidState;
        if (Window != null) return ManagedUiResult.ResourceLimit;
        if (title.Length == 0 || title.Length > 127 || outerWidth <= 0 ||
            outerHeight <= GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight)
            return ManagedUiResult.InvalidArgument;

        GuideXosResult result = _host.TryCreateSurface(title, outerWidth,
            outerHeight, out GuideXosSurface surface);
        if (result != GuideXosResult.Success || surface == null)
            return result == GuideXosResult.Success
                ? ManagedUiResult.WindowCreationFailed
                : GuideXosC185ResultMap.ToManaged(result);

        Window = new GuideXosC185WindowAdapter(surface, _identity, _owner,
            outerWidth, outerHeight);
        window = Window;
        return ManagedUiResult.Success;
    }

    internal void Release()
    {
        if (_released) return;
        _released = true;
        Window?.ReleaseFromHost();
        Window = null;
    }
}

internal sealed class GuideXosC185WindowAdapter : IManagedUiWindow
{
    private const int ButtonControlCapacity = 1;
    private readonly GuideXosC185ApplicationBridge _owner;
    private readonly GuideXosC185SurfaceAdapter _surface;
    private readonly GuideXosControlHost _controls;
    private GuideXosLabelAdapter _label;
    private GuideXosButtonAdapter _button;
    private uint _buttonCommandId;
    private GuideXosSurface _nativeSurface;
    private int _outerWidth;
    private int _outerHeight;
    private bool _closed;

    internal GuideXosC185WindowAdapter(GuideXosSurface nativeSurface,
        ManagedUiApplicationIdentity identity,
        GuideXosC185ApplicationBridge owner, int outerWidth, int outerHeight)
    {
        _nativeSurface = nativeSurface;
        _owner = owner;
        _outerWidth = outerWidth;
        _outerHeight = outerHeight;
        Identity = new ManagedUiWindowIdentity(identity.ApplicationId,
            identity.Generation, 1u);
        _surface = new GuideXosC185SurfaceAdapter(this);
        _controls = new GuideXosControlHost(ButtonControlCapacity);
    }

    public ManagedUiWindowIdentity Identity { get; }
    public int OuterWidth => _outerWidth;
    public int OuterHeight => _outerHeight;
    public int ClientWidth => _outerWidth;
    public int ClientHeight => _outerHeight -
        GuideXosManagedWindowResizeBoundsC174.NativeTitlebarHeight;
    public IManagedUiSurface Surface => _surface;
    internal bool IsClosed => _closed;

    public ManagedUiResult TryCreateLabel(int x, int y, int width,
        ReadOnlySpan<byte> text, out IManagedUiLabel label)
    {
        label = null;
        if (!IsOpen) return ManagedUiResult.InvalidState;
        if (_label != null) return ManagedUiResult.ResourceLimit;
        if (x < 0 || y < 0 || width <= 0 || width % GuideXosLabel.CharacterWidth != 0)
            return ManagedUiResult.InvalidArgument;
        Span<char> characters = stackalloc char[GuideXosLabel.MaximumSupportedTextLength];
        if (!GuideXosC185Utf8.TryDecode(text, characters, out int length))
            return ManagedUiResult.InvalidArgument;

        GuideXosLabel control = new(x, y, width, string.Empty,
            GuideXosLabel.MaximumSupportedTextLength);
        if (!control.SetText(characters[..length]))
            return ManagedUiResult.InvalidArgument;
        _label = new GuideXosLabelAdapter(this, control);
        label = _label;
        return ManagedUiResult.Success;
    }

    public ManagedUiResult TryCreateButton(uint commandId, int x, int y,
        int width, int height, ReadOnlySpan<byte> label,
        out IManagedUiButton button)
    {
        button = null;
        if (!IsOpen) return ManagedUiResult.InvalidState;
        if (_button != null) return ManagedUiResult.ResourceLimit;
        if (commandId == 0u || commandId > int.MaxValue || x < 0 || y < 0 ||
            width <= 0 || height <= 0)
            return ManagedUiResult.InvalidArgument;

        Span<char> characters = stackalloc char[GuideXosButton.MaximumSupportedLabelLength];
        if (!GuideXosC185Utf8.TryDecode(label, characters, out int length))
            return ManagedUiResult.InvalidArgument;
        GuideXosButton control = new(0, 0, width, height, string.Empty,
            GuideXosButton.MaximumSupportedLabelLength);
        if (!control.SetLabel(new string(characters[..length])) ||
            !control.TrySetBounds(x, y, width, height))
            return ManagedUiResult.InvalidArgument;
        if (_controls.TryRegisterButton((int)commandId, control) !=
            GuideXosControlHostResult.Registered)
            return ManagedUiResult.ResourceLimit;

        _buttonCommandId = commandId;
        _button = new GuideXosButtonAdapter(this, control);
        button = _button;
        return ManagedUiResult.Success;
    }

    public ManagedUiResult RouteInput(in ManagedUiInputEvent input,
        out uint activatedCommandId)
    {
        activatedCommandId = 0u;
        if (!IsOpen) return ManagedUiResult.InvalidState;
        if (_button == null) return ManagedUiResult.Unsupported;

        GuideXosControlHostResult result;
        if (input.Kind == ManagedUiInputKind.PointerDown &&
            input.Button == ManagedUiPointerButton.Primary)
        {
            GuideXosButton button = _button.Control;
            if (input.X < button.X || input.Y < button.Y ||
                input.X >= button.X + button.Width ||
                input.Y >= button.Y + button.Height)
                return ManagedUiResult.Success;
            result = _controls.FocusAndRoutePointer((int)_buttonCommandId,
                input.X, input.Y);
        }
        else if (input.Kind == ManagedUiInputKind.KeyDown)
        {
            result = _controls.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosC185InputMap.ToHostKey(input.Key), input.Shift,
                input.Control));
        }
        else if (input.Kind == ManagedUiInputKind.KeyCharacter)
        {
            result = _controls.HandleInput(GuideXosInputEvent.ForKeyChar(
                input.Character, input.Shift, input.Control));
        }
        else
        {
            return ManagedUiResult.Success;
        }

        if (result == GuideXosControlHostResult.Activated)
            activatedCommandId = _buttonCommandId;
        return result == GuideXosControlHostResult.Rejected
            ? ManagedUiResult.InvalidArgument : ManagedUiResult.Success;
    }

    public ManagedUiResult RequestRedraw() => IsOpen
        ? GuideXosC185ResultMap.ToManaged(_owner.RenderForWindow())
        : ManagedUiResult.InvalidState;

    public ManagedUiResult RequestActivation() => IsOpen
        ? ManagedUiResult.Unsupported
        : ManagedUiResult.InvalidState;

    public ManagedUiResult TryClose()
    {
        if (_closed) return ManagedUiResult.Success;
        if (_nativeSurface == null) return ManagedUiResult.InvalidState;
        GuideXosResult result = _nativeSurface.TryClose();
        if (result == GuideXosResult.Success)
        {
            _closed = true;
            _controls.Reset();
        }
        return GuideXosC185ResultMap.ToManaged(result);
    }

    internal bool IsOpen => !_closed && _nativeSurface != null;
    internal GuideXosSurface NativeSurface => IsOpen ? _nativeSurface : null;

    internal void SetGeometry(int outerWidth, int outerHeight)
    {
        if (!IsOpen || outerWidth <= 0 || outerHeight <= 24) return;
        _outerWidth = outerWidth;
        _outerHeight = outerHeight;
    }

    internal void ReleaseFromHost()
    {
        if (_closed) return;
        _controls.Reset();
        if (_nativeSurface != null) _ = _nativeSurface.TryClose();
        _closed = true;
        _nativeSurface = null;
    }
}

internal sealed class GuideXosC185SurfaceAdapter : IManagedUiSurface
{
    private readonly GuideXosC185WindowAdapter _window;

    internal GuideXosC185SurfaceAdapter(GuideXosC185WindowAdapter window)
    {
        _window = window;
    }

    public int Width => _window.IsOpen ? _window.OuterWidth : 0;
    public int Height => _window.IsOpen ? _window.OuterHeight : 0;

    public ManagedUiResult TryFillRect(int x, int y, int width, int height,
        uint color)
    {
        if (!IsBoundedRect(x, y, width, height))
            return _window.IsOpen ? ManagedUiResult.InvalidArgument :
                ManagedUiResult.InvalidState;
        return GuideXosC185ResultMap.ToManaged(
            _window.NativeSurface.TryFillRect(x, y, width, height, color));
    }

    public ManagedUiResult TryDrawText(int x, int y,
        ReadOnlySpan<byte> utf8Text)
    {
        if (!_window.IsOpen) return ManagedUiResult.InvalidState;
        if (x < 0 || y < 0 || utf8Text.Length > GuideXosSurface.MaximumTextLength ||
            x + (long)utf8Text.Length * GuideXosLabel.CharacterWidth > Width ||
            y + GuideXosLabel.TextRowHeight > Height)
            return ManagedUiResult.InvalidArgument;
        return GuideXosC185ResultMap.ToManaged(
            _window.NativeSurface.TrySetText(x, y, utf8Text));
    }

    public ManagedUiResult TrySetClip(int x, int y, int width, int height)
    {
        if (!_window.IsOpen) return ManagedUiResult.InvalidState;
        if (!IsBoundedRect(x, y, width, height))
            return ManagedUiResult.InvalidArgument;
        _window.NativeSurface.SetRenderClip(x, y, width, height);
        return ManagedUiResult.Success;
    }

    public ManagedUiResult ResetClip()
    {
        if (!_window.IsOpen) return ManagedUiResult.InvalidState;
        _window.NativeSurface.SetRenderClip(0, 0, 0, 0);
        return ManagedUiResult.Success;
    }

    private bool IsBoundedRect(int x, int y, int width, int height) =>
        _window.IsOpen && x >= 0 && y >= 0 && width > 0 && height > 0 &&
        x + (long)width <= Width && y + (long)height <= Height;
}

internal sealed class GuideXosLabelAdapter : IManagedUiLabel
{
    private readonly GuideXosC185WindowAdapter _window;
    private readonly GuideXosLabel _control;

    internal GuideXosLabelAdapter(GuideXosC185WindowAdapter window,
        GuideXosLabel control)
    {
        _window = window;
        _control = control;
    }

    internal GuideXosLabel Control => _control;
    public int X => _control.X;
    public int Y => _control.Y;
    public int Width => _control.Width;
    public ManagedUiResult SetText(ReadOnlySpan<byte> utf8Text)
    {
        if (!_window.IsOpen) return ManagedUiResult.InvalidState;
        Span<char> characters = stackalloc char[GuideXosLabel.MaximumSupportedTextLength];
        return GuideXosC185Utf8.TryDecode(utf8Text, characters, out int length) &&
            _control.SetText(characters[..length])
            ? ManagedUiResult.Success : ManagedUiResult.InvalidArgument;
    }

    public ManagedUiResult TrySetBounds(int x, int y, int width) =>
        !_window.IsOpen ? ManagedUiResult.InvalidState :
        _control.TrySetBounds(x, y, width)
            ? ManagedUiResult.Success : ManagedUiResult.InvalidArgument;

    public ManagedUiResult Render() => !_window.IsOpen
        ? ManagedUiResult.InvalidState
        : GuideXosC185ResultMap.ToManaged(
            _control.Render(_window.NativeSurface));
}

internal sealed class GuideXosButtonAdapter : IManagedUiButton
{
    private readonly GuideXosC185WindowAdapter _window;
    private readonly GuideXosButton _control;

    internal GuideXosButtonAdapter(GuideXosC185WindowAdapter window,
        GuideXosButton control)
    {
        _window = window;
        _control = control;
    }

    internal GuideXosButton Control => _control;
    public int X => _control.X;
    public int Y => _control.Y;
    public int Width => _control.Width;
    public int Height => _control.Height;
    public ManagedUiResult SetLabel(ReadOnlySpan<byte> utf8Text)
    {
        if (!_window.IsOpen) return ManagedUiResult.InvalidState;
        Span<char> characters = stackalloc char[GuideXosButton.MaximumSupportedLabelLength];
        return GuideXosC185Utf8.TryDecode(utf8Text, characters, out int length) &&
            _control.SetLabel(new string(characters[..length]))
            ? ManagedUiResult.Success : ManagedUiResult.InvalidArgument;
    }

    public ManagedUiResult TrySetBounds(int x, int y, int width, int height) =>
        !_window.IsOpen ? ManagedUiResult.InvalidState :
        _control.TrySetBounds(x, y, width, height)
            ? ManagedUiResult.Success : ManagedUiResult.InvalidArgument;

    public ManagedUiResult Render() => !_window.IsOpen
        ? ManagedUiResult.InvalidState
        : GuideXosC185ResultMap.ToManaged(
            _control.Render(_window.NativeSurface));
}

internal static class GuideXosC185ResultMap
{
    internal static GuideXosResult ToHost(ManagedUiResult result) => result switch
    {
        ManagedUiResult.Success => GuideXosResult.Success,
        ManagedUiResult.WindowCreationFailed => GuideXosResult.SurfaceCreationFailed,
        ManagedUiResult.Unsupported => GuideXosResult.InvalidAction,
        ManagedUiResult.InvalidState => GuideXosResult.SurfaceCreationFailed,
        _ => GuideXosResult.InvalidArgument,
    };

    internal static ManagedUiResult ToManaged(GuideXosResult result) => result switch
    {
        GuideXosResult.Success => ManagedUiResult.Success,
        GuideXosResult.CapabilityUnavailable => ManagedUiResult.Unsupported,
        GuideXosResult.SurfaceCreationFailed => ManagedUiResult.WindowCreationFailed,
        GuideXosResult.InvalidAction => ManagedUiResult.Unsupported,
        GuideXosResult.InvalidArgument => ManagedUiResult.DrawingFailed,
        _ => ManagedUiResult.InvalidState,
    };
}

internal static class GuideXosC185InputMap
{
    internal static ManagedUiInputEvent From(GuideXosInputEvent input)
    {
        ManagedUiInputKind kind = input.Kind switch
        {
            GuideXosInputKind.PointerDown => ManagedUiInputKind.PointerDown,
            GuideXosInputKind.PointerUp => ManagedUiInputKind.PointerUp,
            GuideXosInputKind.PointerMove => ManagedUiInputKind.PointerMove,
            GuideXosInputKind.KeyDown => ManagedUiInputKind.KeyDown,
            GuideXosInputKind.KeyChar => ManagedUiInputKind.KeyCharacter,
            _ => ManagedUiInputKind.None,
        };
        ManagedUiPointerButton button = input.Button switch
        {
            GuideXosPointerButton.Primary => ManagedUiPointerButton.Primary,
            GuideXosPointerButton.Secondary => ManagedUiPointerButton.Secondary,
            _ => ManagedUiPointerButton.None,
        };
        return new ManagedUiInputEvent(kind, input.X, input.Y, button,
            FromHostKey((GuideXosTextInputKey)input.KeyCode),
            input.Character, input.Shift, input.Control);
    }

    internal static GuideXosTextInputKey ToHostKey(ManagedUiKey key) => key switch
    {
        ManagedUiKey.Enter => GuideXosTextInputKey.Enter,
        ManagedUiKey.Tab => GuideXosTextInputKey.Tab,
        ManagedUiKey.Escape => GuideXosTextInputKey.Escape,
        ManagedUiKey.Backspace => GuideXosTextInputKey.Backspace,
        ManagedUiKey.Delete => GuideXosTextInputKey.Delete,
        ManagedUiKey.Left => GuideXosTextInputKey.Left,
        ManagedUiKey.Right => GuideXosTextInputKey.Right,
        ManagedUiKey.Up => GuideXosTextInputKey.Up,
        ManagedUiKey.Down => GuideXosTextInputKey.Down,
        ManagedUiKey.Home => GuideXosTextInputKey.Home,
        ManagedUiKey.End => GuideXosTextInputKey.End,
        _ => (GuideXosTextInputKey)0u,
    };

    private static ManagedUiKey FromHostKey(GuideXosTextInputKey key) => key switch
    {
        GuideXosTextInputKey.Enter => ManagedUiKey.Enter,
        GuideXosTextInputKey.Tab => ManagedUiKey.Tab,
        GuideXosTextInputKey.Escape => ManagedUiKey.Escape,
        GuideXosTextInputKey.Backspace => ManagedUiKey.Backspace,
        GuideXosTextInputKey.Delete => ManagedUiKey.Delete,
        GuideXosTextInputKey.Left => ManagedUiKey.Left,
        GuideXosTextInputKey.Right => ManagedUiKey.Right,
        GuideXosTextInputKey.Up => ManagedUiKey.Up,
        GuideXosTextInputKey.Down => ManagedUiKey.Down,
        GuideXosTextInputKey.Home => ManagedUiKey.Home,
        GuideXosTextInputKey.End => ManagedUiKey.End,
        _ => ManagedUiKey.None,
    };
}

internal static class GuideXosC185Utf8
{
    internal static bool TryDecode(ReadOnlySpan<byte> utf8,
        Span<char> destination, out int charsWritten)
    {
        charsWritten = 0;
        if (utf8.Length == 0) return true;
        try
        {
            charsWritten = Encoding.UTF8.GetChars(utf8, destination);
            return true;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }
}

internal static class GuideXosManagedHostServicesC185Composition
{
    private const uint ApplicationLifetimeKey = CounterApplicationFactory.StableApplicationId;
    private static readonly IManagedUiApplicationFactory[] s_factories =
    {
        new CounterApplicationFactory(),
    };
    private static readonly GuideXosApplicationDescriptor s_descriptor =
        new(ApplicationLifetimeKey, "C185 Managed Counter"u8,
            static () => new GuideXosC185ApplicationBridge(
                s_factories[0].Create()));
    private static readonly GuideXosC185Runtime s_runtime = new();

    internal static int RegisteredFactoryCount => s_factories.Length;
    internal static bool HasFactory(uint applicationId)
    {
        for (int index = 0; index < s_factories.Length; index++)
            if (s_factories[index].ApplicationId == applicationId) return true;
        return false;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static unsafe int Dispatch(NativeGxAppContext* context)
    {
        if (context == null || context->size < GxAbi.LegacyContextSize)
            return GxAbi.ErrorInvalidArgument;
        if (!GuideXosHost.TryCreate(context, ApplicationLifetimeKey,
                out GuideXosHost host, out GuideXosResult hostResult) || host == null)
            return hostResult == GuideXosResult.AbiIncompatible
                ? GxAbi.ErrorUnsupported : (int)hostResult;
        return (int)s_runtime.Dispatch(host);
    }

    internal static GuideXosApplicationDescriptor Descriptor => s_descriptor;
    internal static GuideXosC185Runtime CreateRuntimeForTests() => new();
}

internal sealed class GuideXosC185Runtime
{
    private readonly GuideXosManagedApplicationLifetime _lifetime = new();

    internal GuideXosC185ApplicationBridge CurrentBridgeForTests
    {
        get
        {
            return _lifetime.TryGet(
                CounterApplicationFactory.StableApplicationId,
                out GuideXosApplication app)
                ? app as GuideXosC185ApplicationBridge : null;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal GuideXosResult Dispatch(GuideXosHost host)
    {
        if (host == null) return GuideXosResult.InvalidArgument;
        uint generation = host.LaunchContext.ApplicationGeneration;
        bool isLaunch = !host.IsAction && !host.LaunchContext.IsInput;
        GuideXosApplication application;

        if (isLaunch)
        {
            if (!_lifetime.TryStart(GuideXosManagedHostServicesC185Composition.Descriptor,
                    generation, out application, out generation) || application == null)
                return GuideXosResult.InvalidArgument;
            ((GuideXosC185ApplicationBridge)application).SetIdentity(
                CounterApplicationFactory.StableApplicationId, generation);
            GuideXosResult launch = application.Launch(host);
            if (launch != GuideXosResult.Success)
                _ = _lifetime.Clear(CounterApplicationFactory.StableApplicationId,
                    generation);
            return launch;
        }

        if (!_lifetime.TryGet(CounterApplicationFactory.StableApplicationId,
                generation, out application) || application == null)
            return GuideXosResult.InvalidArgument;

        if (host.IsAction && host.LaunchContext.ActionId ==
            GuideXosApplicationRegistry.LifetimeCloseActionId)
        {
            return _lifetime.Clear(CounterApplicationFactory.StableApplicationId,
                generation) ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }
        if (host.LaunchContext.IsInput)
            return application.HandleInput(host,
                GuideXosInputEvent.From(host.LaunchContext));
        if (host.IsAction)
            return GuideXosManagedWindowResizeDispatchC180.DispatchAction(
                application, host, host.LaunchContext.ActionId);
        return GuideXosResult.InvalidArgument;
    }

    internal bool Clear(uint generation) => _lifetime.Clear(
        CounterApplicationFactory.StableApplicationId, generation);

    internal bool TryGet(uint generation, out GuideXosC185ApplicationBridge bridge)
    {
        bool found = _lifetime.TryGet(
            CounterApplicationFactory.StableApplicationId, generation,
            out GuideXosApplication application);
        bridge = found ? application as GuideXosC185ApplicationBridge : null;
        return bridge != null;
    }
}
#endif
