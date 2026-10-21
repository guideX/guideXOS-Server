namespace GuideXos.ManagedUi;

/// <summary>Result of one synchronous managed host-service operation.</summary>
/// <remarks>
/// InvalidState means the owning application or window is not active;
/// Unsupported means the host does not provide that operation; and
/// ApplicationFailure reports an application callback that failed. Native
/// host ABI errors may collapse these values to its existing error codes.
/// </remarks>
public enum ManagedUiResult : byte
{
    Success,
    InvalidArgument,
    InvalidState,
    Unsupported,
    ResourceLimit,
    WindowCreationFailed,
    DrawingFailed,
    ApplicationFailure,
}

public enum ManagedUiInputKind : byte
{
    None,
    PointerDown,
    PointerUp,
    PointerMove,
    KeyDown,
    KeyCharacter,
}

public enum ManagedUiPointerButton : byte
{
    None,
    Primary,
    Secondary,
}

public enum ManagedUiKey : byte
{
    None,
    Enter,
    Tab,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
}

/// <summary>
/// Identifies one running application. ApplicationId is the registered
/// application type ID; Generation distinguishes each running lifetime.
/// </summary>
public readonly struct ManagedUiApplicationIdentity
{
    public ManagedUiApplicationIdentity(uint applicationId, uint generation)
    {
        ApplicationId = applicationId;
        Generation = generation;
    }

    public uint ApplicationId { get; }
    public uint ApplicationTypeId => ApplicationId;
    public uint Generation { get; }
}

/// <summary>
/// Identifies one window owned by an application lifetime. Ordinal is local
/// to that lifetime; this contract currently creates ordinal 1 only.
/// </summary>
public readonly struct ManagedUiWindowIdentity
{
    public ManagedUiWindowIdentity(uint applicationId, uint generation, uint ordinal)
    {
        ApplicationId = applicationId;
        Generation = generation;
        Ordinal = ordinal;
    }

    public uint ApplicationId { get; }
    public uint ApplicationTypeId => ApplicationId;
    public uint Generation { get; }
    public uint Ordinal { get; }
}

/// <summary>
/// Application opt-in bounds for the shared C174/C180 window resize path.
/// Returning false from TryGetResizeBounds denies resize for that dispatch.
/// Bounds describe outer window dimensions and must pass IsValid.
/// </summary>
public readonly struct ManagedUiResizeBounds
{
    public ManagedUiResizeBounds(int minimumOuterWidth, int minimumOuterHeight,
        int maximumOuterWidth, int maximumOuterHeight)
    {
        MinimumOuterWidth = minimumOuterWidth;
        MinimumOuterHeight = minimumOuterHeight;
        MaximumOuterWidth = maximumOuterWidth;
        MaximumOuterHeight = maximumOuterHeight;
    }

    public int MinimumOuterWidth { get; }
    public int MinimumOuterHeight { get; }
    public int MaximumOuterWidth { get; }
    public int MaximumOuterHeight { get; }

    public bool IsValid => MinimumOuterWidth > 0 && MinimumOuterHeight > 24 &&
        MaximumOuterWidth >= MinimumOuterWidth &&
        MaximumOuterHeight >= MinimumOuterHeight;
}

/// <summary>
/// One synchronous resize candidate from the shared host transaction.
/// Client dimensions use the host's existing title-bar geometry rules.
/// </summary>
public readonly struct ManagedUiResizeProposal
{
    public ManagedUiResizeProposal(int priorOuterWidth, int priorOuterHeight,
        int outerWidth, int outerHeight, int clientWidth, int clientHeight)
    {
        PriorOuterWidth = priorOuterWidth;
        PriorOuterHeight = priorOuterHeight;
        OuterWidth = outerWidth;
        OuterHeight = outerHeight;
        ClientWidth = clientWidth;
        ClientHeight = clientHeight;
    }

    public int PriorOuterWidth { get; }
    public int PriorOuterHeight { get; }
    public int OuterWidth { get; }
    public int OuterHeight { get; }
    public int ClientWidth { get; }
    public int ClientHeight { get; }
}

/// <summary>
/// Application's decision for the current resize proposal. The host accepts
/// the geometry only when layout, rendering, and application state all pass;
/// otherwise it restores the prior geometry and completes the transaction as
/// rejected.
/// </summary>
public readonly struct ManagedUiResizeOutcome
{
    public ManagedUiResizeOutcome(bool layoutAccepted,
        ManagedUiResult renderResult, bool stateAccepted)
    {
        LayoutAccepted = layoutAccepted;
        RenderResult = renderResult;
        StateAccepted = stateAccepted;
    }

    public bool LayoutAccepted { get; }
    public ManagedUiResult RenderResult { get; }
    public bool StateAccepted { get; }
}

/// <summary>
/// A copied input event delivered synchronously to an active application.
/// There is no persistent input subscription; the host chooses the target
/// from its current native input routing and application generation.
/// </summary>
public readonly struct ManagedUiInputEvent
{
    public ManagedUiInputEvent(ManagedUiInputKind kind, int x, int y,
        ManagedUiPointerButton button = ManagedUiPointerButton.None,
        ManagedUiKey key = ManagedUiKey.None, char character = '\0',
        bool shift = false, bool control = false)
    {
        Kind = kind;
        X = x;
        Y = y;
        Button = button;
        Key = key;
        Character = character;
        Shift = shift;
        Control = control;
    }

    public ManagedUiInputKind Kind { get; }
    public int X { get; }
    public int Y { get; }
    public ManagedUiPointerButton Button { get; }
    public ManagedUiKey Key { get; }
    public char Character { get; }
    public bool Shift { get; }
    public bool Control { get; }
}

/// <summary>
/// A statically registered application factory. ApplicationId identifies
/// the application type, not an individual running instance.
/// </summary>
public interface IManagedUiApplicationFactory
{
    uint ApplicationId { get; }
    IManagedUiApplication Create();
}

/// <summary>
/// One application instance. Initialize is called once. Ordinary input,
/// actions, rendering, and resize callbacks are delivered only after it
/// returns Success. Any other result or an exception fails initialization;
/// the host calls OnTearingDown once to release partially acquired resources.
/// After teardown begins, no ordinary callback is delivered.
/// </summary>
public interface IManagedUiApplication
{
    ManagedUiResult Initialize(IManagedUiHostServices services,
        in ManagedUiApplicationIdentity identity);
    ManagedUiResult Render();
    ManagedUiResult HandleAction(uint actionId);
    ManagedUiResult HandleInput(in ManagedUiInputEvent input);
    /// <summary>
    /// Opts this active window into the shared resize transaction by returning
    /// valid bounds. Returning false or invalid bounds denies resizing.
    /// </summary>
    bool TryGetResizeBounds(out ManagedUiResizeBounds bounds);
    /// <summary>Applies the proposed layout tentatively and reports its result.</summary>
    ManagedUiResizeOutcome HandleResize(in ManagedUiResizeProposal proposal);
    /// <summary>
    /// Finalizes a resize proposal after the host accepts or rolls it back.
    /// The application must restore tentative layout state when accepted is
    /// false. No resize callback is delivered after closing begins.
    /// </summary>
    void CompleteResize(bool accepted);
    void OnTearingDown();
}

/// <summary>
/// Host-owned services scoped to one application generation. Implementations
/// may reject creation when the application already owns its supported
/// window or when required host capabilities are unavailable.
/// </summary>
public interface IManagedUiHostServices
{
    ManagedUiResult TryCreateWindow(ReadOnlySpan<byte> title,
        int outerWidth, int outerHeight, out IManagedUiWindow window);
}

/// <summary>
/// A host-owned window scoped to one application generation. A successful
/// TryClose invalidates the window and all wrappers obtained from it. Calls
/// through cached wrappers then return InvalidState; they never expose or
/// reuse a native handle.
/// </summary>
public interface IManagedUiWindow
{
    ManagedUiWindowIdentity Identity { get; }
    int OuterWidth { get; }
    int OuterHeight { get; }
    int ClientWidth { get; }
    int ClientHeight { get; }
    IManagedUiSurface Surface { get; }
    ManagedUiResult TryCreateLabel(int x, int y, int width,
        ReadOnlySpan<byte> text, out IManagedUiLabel label);
    ManagedUiResult TryCreateButton(uint commandId, int x, int y,
        int width, int height, ReadOnlySpan<byte> label,
        out IManagedUiButton button);
    ManagedUiResult RouteInput(in ManagedUiInputEvent input,
        out uint activatedCommandId);
    ManagedUiResult RequestRedraw();
    /// <summary>
    /// Requests OS window activation. The current host returns Unsupported;
    /// callers must not treat a managed-only focus change as activation.
    /// </summary>
    ManagedUiResult RequestActivation();
    /// <summary>
    /// Closes this window. Success makes the window and its wrappers inert;
    /// a failed close leaves the live window available for retry.
    /// </summary>
    ManagedUiResult TryClose();
}

/// <summary>
/// A checked drawing view over the host-owned window surface. The view is
/// usable only while its window is open and its application is active. The
/// host allocates and releases the underlying surface; callers may retain the
/// managed view, but every operation after invalidation fails with
/// InvalidState. Drawing and redraw are synchronous.
/// </summary>
public interface IManagedUiSurface
{
    int Width { get; }
    int Height { get; }
    ManagedUiResult TryFillRect(int x, int y, int width, int height,
        uint color);
    ManagedUiResult TryDrawText(int x, int y, ReadOnlySpan<byte> utf8Text);
    ManagedUiResult TrySetClip(int x, int y, int width, int height);
    ManagedUiResult ResetClip();
}

/// <summary>
/// Host-created label control. Its wrapper is scoped to its window and
/// becomes inert when that window closes.
/// </summary>
public interface IManagedUiLabel
{
    int X { get; }
    int Y { get; }
    int Width { get; }
    ManagedUiResult SetText(ReadOnlySpan<byte> utf8Text);
    ManagedUiResult TrySetBounds(int x, int y, int width);
    ManagedUiResult Render();
}

/// <summary>
/// Host-created button control. Its wrapper is scoped to its window and
/// becomes inert when that window closes.
/// </summary>
public interface IManagedUiButton
{
    int X { get; }
    int Y { get; }
    int Width { get; }
    int Height { get; }
    ManagedUiResult SetLabel(ReadOnlySpan<byte> utf8Text);
    ManagedUiResult TrySetBounds(int x, int y, int width, int height);
    ManagedUiResult Render();
}
