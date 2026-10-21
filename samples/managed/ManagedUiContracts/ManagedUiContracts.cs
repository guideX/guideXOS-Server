namespace GuideXos.ManagedUi;

public enum ManagedUiResult : byte
{
    Success,
    InvalidArgument,
    InvalidState,
    Unsupported,
    ResourceLimit,
    WindowCreationFailed,
    DrawingFailed,
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

public readonly struct ManagedUiApplicationIdentity
{
    public ManagedUiApplicationIdentity(uint applicationId, uint generation)
    {
        ApplicationId = applicationId;
        Generation = generation;
    }

    public uint ApplicationId { get; }
    public uint Generation { get; }
}

public readonly struct ManagedUiWindowIdentity
{
    public ManagedUiWindowIdentity(uint applicationId, uint generation, uint ordinal)
    {
        ApplicationId = applicationId;
        Generation = generation;
        Ordinal = ordinal;
    }

    public uint ApplicationId { get; }
    public uint Generation { get; }
    public uint Ordinal { get; }
}

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

public interface IManagedUiApplicationFactory
{
    uint ApplicationId { get; }
    IManagedUiApplication Create();
}

public interface IManagedUiApplication
{
    ManagedUiResult Initialize(IManagedUiHostServices services,
        in ManagedUiApplicationIdentity identity);
    ManagedUiResult Render();
    ManagedUiResult HandleAction(uint actionId);
    ManagedUiResult HandleInput(in ManagedUiInputEvent input);
    bool TryGetResizeBounds(out ManagedUiResizeBounds bounds);
    ManagedUiResizeOutcome HandleResize(in ManagedUiResizeProposal proposal);
    void CompleteResize(bool accepted);
    void OnTearingDown();
}

public interface IManagedUiHostServices
{
    ManagedUiResult TryCreateWindow(ReadOnlySpan<byte> title,
        int outerWidth, int outerHeight, out IManagedUiWindow window);
}

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
    ManagedUiResult RequestActivation();
    ManagedUiResult TryClose();
}

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

public interface IManagedUiLabel
{
    int X { get; }
    int Y { get; }
    int Width { get; }
    ManagedUiResult SetText(ReadOnlySpan<byte> utf8Text);
    ManagedUiResult TrySetBounds(int x, int y, int width);
    ManagedUiResult Render();
}

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
