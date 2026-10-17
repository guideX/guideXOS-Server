using Astronomy.Core;
using Astronomy.Time;
using Navigation.Core;

namespace Astronomy.Ephemeris;

/// <summary>
/// SN6 framed-state service: consumes the existing SN4 ephemeris provider
/// (J2000/SSB geometric states; numerical evaluation untouched) and produces
/// correctly labeled derived states in supported orientations and origins.
///
/// Order of operations for e.g. Mars relative to Earth in Earth-fixed axes:
/// 1. origin translation (Mars SSB-rel → Earth-rel, J2000 axes unchanged),
/// 2. frame orientation + velocity transformation (J2000 → IAU_EARTH).
/// SSB-centered geometric states are never confused with Earth-centered
/// states: the origin id travels with every result.
///
/// AstroTime compatibility inputs use their JD-derived ET. UTC inputs retain
/// the exact SN5 ET through the optional precision-preserving provider path.
/// UTC labels are accepted ONLY through the SN5 <see cref="TimeConverter"/>
/// policies; provisional epochs downgrade the result to
/// <see cref="NavigationResultStatus.StaleData"/> (never silent).
/// Simulated spacecraft contributions stay simulation-derived: a validated
/// transform never promotes them to ValidatedReference.
/// </summary>
public sealed class FramedStateService
{
    private readonly IEphemerisProvider _provider;
    private readonly IauEarthOrientation? _orientationModel;

    public FramedStateService(IEphemerisProvider provider, IauEarthOrientation? orientationModel = null)
    {
        _provider = provider ?? throw new ArgumentNullException(nameof(provider));
        _orientationModel = orientationModel;
    }

    public string DataSource => _provider.DataSource + " + SN6 framed states (IAU_EARTH pck00011.tpc)";

    private static double EtFromTdbLabel(AstroTime epoch)
    {
        if (!double.IsFinite(epoch.JdUtc))
            throw new ArgumentException("Epoch JD must be finite.", nameof(epoch));
        if (epoch.OriginalScale != TimeScale.Tdb)
            throw new ArgumentException(
                $"Framed queries require a TDB epoch; got {epoch.OriginalScale}. " +
                "Convert UTC via the SN5 TimeConverter first.", nameof(epoch));
        return (epoch.JdUtc - 2451545.0) * 86400.0;
    }

    private CartesianState ToCartesian(string bodyId,
        Vector3D pos, Vector3D vel, AstroTime epoch, double et,
        NavigationResultStatus quality, string provenance) =>
        new()
        {
            Position = pos,
            Velocity = vel,
            Orientation = FrameOrientation.J2000,
            OriginId = "ssb",
            Epoch = epoch,
            EtSeconds = et,
            Provenance = provenance,
            IsSimulated = false,
            Quality = quality,
        };

    /// <summary>
    /// State of <paramref name="bodyId"/> in <paramref name="orientation"/>
    /// axes relative to <paramref name="originId"/> ("ssb" or a supported
    /// body id). Orientation/origin are independent: IAU_EARTH axes may be
    /// combined with any supported origin, recorded in provenance.
    /// </summary>
    public NavigationResult<CartesianState> GetState(
        string bodyId, AstroTime epoch, FrameOrientation orientation, string originId)
    {
        double et;
        try
        {
            et = EtFromTdbLabel(epoch);
        }
        catch (ArgumentException ex)
        {
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, ex.Message);
        }
        return GetStateCore(bodyId, epoch, et, orientation, originId, requireEtProvider: false);
    }

    /// <summary>Evaluate a framed state at ET without converting through a Julian date.</summary>
    public NavigationResult<CartesianState> GetStateAtEt(
        string bodyId, EtSeconds epoch, FrameOrientation orientation, string originId)
    {
        if (!double.IsFinite(epoch.Value))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput,
                "Epoch ET must be finite seconds past J2000 (TDB).");
        var label = new AstroTime(epoch.ToJulianDateTdb(), TimeScale.Tdb);
        return GetStateCore(bodyId, label, epoch.Value, orientation, originId, requireEtProvider: true);
    }

    private NavigationResult<CartesianState> GetStateCore(
        string bodyId, AstroTime epoch, double et, FrameOrientation orientation, string originId,
        bool requireEtProvider)
    {
        if (string.IsNullOrWhiteSpace(bodyId))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Body id must be provided.");
        if (string.IsNullOrWhiteSpace(originId))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Origin id must be provided.");

        if (originId.StartsWith("spacecraft:", StringComparison.Ordinal))
            return Fail<CartesianState>(NavigationResultStatus.UnsupportedCalculation,
                "Spacecraft origins require GetSpacecraftRelative (explicit simulated-spacecraft handling).");

        if (orientation == FrameOrientation.IauEarth &&
            (_orientationModel is null || !_orientationModel.IsAuthenticated))
            return Fail<CartesianState>(NavigationResultStatus.InsufficientInformation,
                "Validated IAU_EARTH calculations require successfully authenticated earth_orientation.sn6json data.");

        if (requireEtProvider && _provider is not IEtEphemerisProvider)
            return Fail<CartesianState>(NavigationResultStatus.UnsupportedCalculation,
                "This provider does not expose precision-preserving ET evaluation; an ET query cannot be routed through JD.");

        NavigationResult<(Vector3D Position, Vector3D Velocity)> QueryProvider(string body) =>
            requireEtProvider
                ? ((IEtEphemerisProvider)_provider).GetStateAtEt(body, new EtSeconds(et), ReferenceFrame.Barycentric)
                : _provider.GetState(body, epoch, ReferenceFrame.Barycentric);

        var target = QueryProvider(bodyId);
        if (!target.IsSuccess)
            return Fail<CartesianState>(target.Status, $"Target state unavailable: {target.ErrorMessage}");
        var (tPos, tVel) = target.Value!;

        var state = ToCartesian(bodyId, tPos, tVel, epoch, et, target.Status,
            $"J2000/SSB geometric ({_provider.DataSource})");

        // 1. Origin translation (axes unchanged).
        if (!string.Equals(originId, "ssb", StringComparison.Ordinal))
        {
            var origin = QueryProvider(originId);
            if (!origin.IsSuccess)
                return Fail<CartesianState>(origin.Status, $"Origin state unavailable: {origin.ErrorMessage}");
            var (oPos, oVel) = origin.Value!;
            var originState = ToCartesian(originId, oPos, oVel, epoch, et, origin.Status,
                $"J2000/SSB geometric ({_provider.DataSource})");
            try
            {
                state = OriginTranslation.ExpressRelativeTo(state, originState, originId);
            }
            catch (Exception ex) when (ex is InvalidOperationException or ArgumentException)
            {
                return Fail<CartesianState>(NavigationResultStatus.InvalidInput, ex.Message);
            }
        }

        // 2. Frame orientation / state transformation.
        if (orientation == FrameOrientation.J2000)
        {
            return NavigationResult<CartesianState>.Success(state, state.Quality, epoch,
                ReferenceFrame.Barycentric,
                state.Provenance + $" | SN6 origin={originId}, orientation=J2000");
        }

        if (orientation == FrameOrientation.IauEarth)
        {
            _orientationModel!.GetAuthenticatedRotation(et, out var r, out var rDot);
            var (p, v) = StateTransformMath.TransformState(r, rDot, state.Position, state.Velocity);
            var framed = new CartesianState
            {
                Position = p,
                Velocity = v,
                Orientation = FrameOrientation.IauEarth,
                OriginId = state.OriginId,
                Epoch = epoch,
                EtSeconds = et,
                Provenance = state.Provenance +
                    $" | SN6 J2000->IAU_EARTH state transform at ET={et:F6}s ({_orientationModel.Provenance})",
                IsSimulated = state.IsSimulated,
                Quality = state.Quality,
            };
            return NavigationResult<CartesianState>.Success(framed, framed.Quality, epoch,
                ReferenceFrame.Barycentric, framed.Provenance);
        }

        return Fail<CartesianState>(NavigationResultStatus.UnsupportedCalculation,
            FrameRegistry.UnsupportedDiagnostic(orientation.ToString()));
    }

    /// <summary>
    /// Name-based orientation overload: unsupported names are typed failures
    /// with a diagnostic (never a silent J2000 fallback).
    /// </summary>
    public NavigationResult<CartesianState> GetState(
        string bodyId, AstroTime epoch, string orientationName, string originId)
    {
        if (!FrameRegistry.TryParseOrientation(orientationName, out var orientation))
            return Fail<CartesianState>(NavigationResultStatus.UnsupportedCalculation,
                FrameRegistry.UnsupportedDiagnostic(orientationName));
        return GetState(bodyId, epoch, orientation, originId);
    }

    /// <summary>
    /// UTC query through SN5 policies. Provisional epochs downgrade an
    /// otherwise validated state to StaleData.
    /// </summary>
    public NavigationResult<CartesianState> GetStateUtc(
        string bodyId, string utcText, TimeConverter converter,
        FrameOrientation orientation, string originId, bool allowProvisionalFuture = false)
    {
        ArgumentNullException.ThrowIfNull(converter);
        if (!converter.TryUtcToEt(utcText, out var time, out var timeError, allowProvisionalFuture))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput,
                $"Time conversion failed [{timeError!.Quality}]: {timeError.Message}");

        var t = time!;
        var epoch = new AstroTime(t.JdTdb, TimeScale.Tdb);
        var result = GetStateAtEt(bodyId, t.Et, orientation, originId);
        if (result.IsSuccess && t.Quality == TimeConversionQuality.ProvisionalStaleTable)
        {
            var s = result.Value!;
            var downgraded = new CartesianState
            {
                Position = s.Position,
                Velocity = s.Velocity,
                Orientation = s.Orientation,
                OriginId = s.OriginId,
                Epoch = s.Epoch,
                EtSeconds = s.EtSeconds,
                Provenance = s.Provenance + " | epoch provisional (stale leap-second table)",
                IsSimulated = s.IsSimulated,
                Quality = NavigationResultStatus.StaleData,
            };
            return NavigationResult<CartesianState>.Success(downgraded,
                NavigationResultStatus.StaleData, epoch, result.Frame,
                downgraded.Provenance + $" | time quality={t.Quality}, ET={t.Et.Value:F9}s");
        }
        return result;
    }

    /// <summary>
    /// Target relative to an explicit spacecraft Cartesian state (same epoch
    /// label). The result uses the J2000-aligned spacecraft-relative
    /// translated frame and stays simulation-derived when the spacecraft
    /// contribution is simulated.
    /// </summary>
    public NavigationResult<CartesianState> GetSpacecraftRelative(
        string targetBodyId, CartesianState spacecraft, string spacecraftId)
    {
        if (spacecraft is null)
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Missing spacecraft state.");
        if (string.IsNullOrWhiteSpace(spacecraftId))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Spacecraft id must be provided.");
        if (string.IsNullOrWhiteSpace(targetBodyId))
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Target body id must be provided.");

        CartesianState sc = spacecraft;
        // Fetch the target at the spacecraft's ET metadata so both inputs use
        // the same effective epoch, even when its AstroTime JD is quantized.
        var targetAtOrigin = GetStateAtEt(targetBodyId, new EtSeconds(sc.EtSeconds),
            FrameOrientation.J2000, sc.OriginId);
        if (!targetAtOrigin.IsSuccess)
            return Fail<CartesianState>(targetAtOrigin.Status, targetAtOrigin.ErrorMessage!);

        try
        {
            var rel = SpacecraftRelativeState.ComputeRelative(sc, targetAtOrigin.Value!, spacecraftId);
            return NavigationResult<CartesianState>.Success(rel, rel.Quality, sc.Epoch,
                ReferenceFrame.Barycentric, rel.Provenance + $" | target ephemeris: {targetAtOrigin.Value!.Provenance}");
        }
        catch (Exception ex) when (ex is InvalidOperationException or ArgumentException)
        {
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, ex.Message);
        }
    }

    /// <summary>
    /// Target relative to a <see cref="SpacecraftState"/> (simulated unless
    /// measured). Camera coordinates are never consulted.
    /// </summary>
    public NavigationResult<CartesianState> GetSpacecraftRelative(
        string targetBodyId, SpacecraftState spacecraft)
    {
        if (spacecraft is null)
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, "Missing spacecraft state.");
        CartesianState sc;
        try
        {
            sc = SpacecraftRelativeState.FromSpacecraftState(spacecraft);
        }
        catch (Exception ex) when (ex is InvalidOperationException or ArgumentException)
        {
            return Fail<CartesianState>(NavigationResultStatus.InvalidInput, ex.Message);
        }
        return GetSpacecraftRelative(targetBodyId, sc, spacecraft.SpacecraftId);
    }

    private static NavigationResult<T> Fail<T>(NavigationResultStatus status, string message) =>
        NavigationResult<T>.Failure(status, message);
}
