namespace HostLogProof;

/// <summary>
/// Describes an application's outer-window interval and the shared native
/// title-bar height used to derive its managed client area.
/// </summary>
internal readonly struct GuideXosManagedWindowResizeBoundsC174
{
    internal const int NativeTitlebarHeight = 24;

    public GuideXosManagedWindowResizeBoundsC174(int minimumOuterWidth,
        int minimumOuterHeight, int maximumOuterWidth, int maximumOuterHeight)
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
    public int TitlebarHeight => NativeTitlebarHeight;

    public bool TryCreateCandidate(int outerWidth, int outerHeight,
        out GuideXosManagedWindowResizeCandidateC174 candidate)
    {
        candidate = default;
        if (MinimumOuterWidth <= 0 || MinimumOuterHeight <= NativeTitlebarHeight ||
            MaximumOuterWidth < MinimumOuterWidth ||
            MaximumOuterHeight < MinimumOuterHeight ||
            outerWidth < MinimumOuterWidth || outerWidth > MaximumOuterWidth ||
            outerHeight < MinimumOuterHeight || outerHeight > MaximumOuterHeight)
            return false;

        candidate = new GuideXosManagedWindowResizeCandidateC174(
            outerWidth, outerHeight, outerWidth,
            outerHeight - NativeTitlebarHeight);
        return true;
    }
}

/// <summary>A validated, absolute geometry proposal with outer and client sizes.</summary>
internal readonly struct GuideXosManagedWindowResizeCandidateC174
{
    public GuideXosManagedWindowResizeCandidateC174(int outerWidth,
        int outerHeight, int clientWidth, int clientHeight)
    {
        IsValid = true;
        OuterWidth = outerWidth;
        OuterHeight = outerHeight;
        ClientWidth = clientWidth;
        ClientHeight = clientHeight;
    }

    public bool IsValid { get; }
    public int OuterWidth { get; }
    public int OuterHeight { get; }
    public int ClientWidth { get; }
    public int ClientHeight { get; }
}

internal enum GuideXosManagedWindowResizeFailureC174 : byte
{
    None,
    Geometry,
    Layout,
    Render,
    State,
}

/// <summary>
/// Value-type transaction bookkeeping shared by live managed resizes.
/// Application-specific layout and state snapshots remain with each app.
/// </summary>
internal struct GuideXosManagedWindowResizeTransactionC174
{
    private bool _valid;
    private bool _completed;
    private bool _accepted;
    private GuideXosManagedWindowResizeCandidateC174 _candidate;
    private GuideXosManagedWindowResizeFailureC174 _failure;
    private GuideXosResult _result;

    public GuideXosManagedWindowResizeTransactionC174(
        in GuideXosManagedWindowResizeBoundsC174 bounds,
        int priorOuterWidth, int priorOuterHeight,
        int requestedOuterWidth, int requestedOuterHeight)
    {
        _candidate = default;
        bool priorValid = bounds.TryCreateCandidate(priorOuterWidth,
            priorOuterHeight, out _);
        bool requestedValid = bounds.TryCreateCandidate(requestedOuterWidth,
            requestedOuterHeight, out _candidate);
        _valid = priorValid && requestedValid;
        _completed = false;
        _accepted = false;
        PriorOuterWidth = priorOuterWidth;
        PriorOuterHeight = priorOuterHeight;
        _failure = _valid
            ? GuideXosManagedWindowResizeFailureC174.None
            : GuideXosManagedWindowResizeFailureC174.Geometry;
        _result = _valid ? GuideXosResult.Success :
            GuideXosResult.InvalidArgument;
    }

    public int PriorOuterWidth { get; }
    public int PriorOuterHeight { get; }
    public GuideXosManagedWindowResizeCandidateC174 Candidate => _candidate;
    public bool IsValid => _valid;
    public bool IsChanged => _valid &&
        (_candidate.OuterWidth != PriorOuterWidth ||
         _candidate.OuterHeight != PriorOuterHeight);
    public bool IsAccepted => _accepted;
    public bool RequiresRollback => _completed && !_accepted && _valid;
    public GuideXosManagedWindowResizeFailureC174 Failure => _failure;
    public GuideXosResult Result => _result;

    /// <summary>
    /// Completes the common accept/reject decision after app-owned layout,
    /// rendering, and state checks have run. Returns true only on acceptance.
    /// </summary>
    public bool Complete(bool layoutAccepted, GuideXosResult renderResult,
        bool stateAccepted)
    {
        if (!_valid || _completed)
            return false;

        _completed = true;
        if (!layoutAccepted)
        {
            _failure = GuideXosManagedWindowResizeFailureC174.Layout;
            _result = GuideXosResult.InvalidArgument;
            return false;
        }
        if (renderResult != GuideXosResult.Success)
        {
            _failure = GuideXosManagedWindowResizeFailureC174.Render;
            _result = renderResult;
            return false;
        }
        if (!stateAccepted)
        {
            _failure = GuideXosManagedWindowResizeFailureC174.State;
            _result = GuideXosResult.InvalidArgument;
            return false;
        }

        _accepted = true;
        _failure = GuideXosManagedWindowResizeFailureC174.None;
        _result = GuideXosResult.Success;
        return true;
    }
}
