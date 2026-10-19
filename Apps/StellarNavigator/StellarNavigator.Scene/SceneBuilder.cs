using System.Diagnostics;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;

namespace StellarNavigator.Scene;

public sealed class SceneBuildException(string message) : InvalidOperationException(message);

/// <summary>
/// Portable scene builder over authenticated ET-native ephemeris and SN6
/// frame/origin services. Camera and viewport state are never accepted here.
/// </summary>
public sealed class SceneBuilder
{
    public const int TrailIntervalsPerOrbit = 48;
    public const int MaximumTrailSamplesPerObject = TrailIntervalsPerOrbit + 1;
    public const int MaximumTotalTrailSamples = 9 * MaximumTrailSamplesPerObject;
    public const string DemonstrationSpacecraftId = "SN7-DEMO";
    public const string DemonstrationSpacecraftDescription =
        "Synthetic co-moving Earth-orbit initial condition: fixed J2000 offset of 42,164 km and " +
        "3.075 km/s transverse velocity, reinitialized at each requested ET. It is not propagated " +
        "and is not real spacecraft telemetry.";

    private static readonly CelestialObjectDefinition[] CelestialObjects =
    [
        new("sun", "Sun", 10, "Body center (NAIF 10)", null),
        new("mercury", "Mercury", 1, "Mercury system barycenter (NAIF 1; center 199 is coincident under the DE440s convention)", 87.969),
        new("venus", "Venus", 2, "Venus system barycenter (NAIF 2; center 299 is coincident under the DE440s convention)", 224.701),
        new("earth", "Earth", 399, "Earth geocenter (NAIF 399), chained through Earth-Moon barycenter 3", 365.256),
        new("mars", "Mars", 4, "Mars system barycenter (NAIF 4; Mars center 499 unavailable)", 686.980),
        new("jupiter", "Jupiter", 5, "Jupiter system barycenter (NAIF 5; center 599 unavailable)", 4332.59),
        new("saturn", "Saturn", 6, "Saturn system barycenter (NAIF 6; center 699 unavailable)", 10759.22),
        new("uranus", "Uranus", 7, "Uranus system barycenter (NAIF 7; center 799 unavailable)", 30688.5),
        new("neptune", "Neptune", 8, "Neptune system barycenter (NAIF 8; center 899 unavailable)", 60182.0),
        new("moon", "Moon", 301, "Moon body center (NAIF 301), chained through Earth-Moon barycenter 3", 27.322),
    ];

    private readonly IEphemerisProvider _provider;
    private readonly FramedStateService _framedStates;
    private readonly IauEarthOrientation? _orientationModel;

    public SceneBuilder(IEphemerisProvider provider, IauEarthOrientation? orientationModel = null)
    {
        _provider = provider ?? throw new ArgumentNullException(nameof(provider));
        if (provider is not IEtEphemerisProvider)
            throw new ArgumentException(
                "SN7 scenes require IEtEphemerisProvider so requested ET is evaluated without JD reconstruction.",
                nameof(provider));
        _orientationModel = orientationModel is { IsAuthenticated: true } ? orientationModel : null;
        _framedStates = new FramedStateService(provider, _orientationModel);
    }

    public static IReadOnlyList<CelestialObjectDefinition> Definitions => CelestialObjects;

    public NavigationScene Build(SceneBuildRequest request, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (!double.IsFinite(request.Epoch.Value))
            throw new SceneBuildException("The requested ET epoch must be a finite number of TDB seconds past J2000.");
        if (request.Orientation == FrameOrientation.IauEarth && request.OriginMode != SceneOriginMode.Earth)
            throw new SceneBuildException("The SN7 IAU_EARTH demonstration is available only in Earth-relative view.");
        if (request.Orientation == FrameOrientation.IauEarth && _orientationModel is null)
            throw new SceneBuildException(
                "Earth-fixed view is disabled: authenticated earth_orientation.sn6json data did not load. J2000 views remain available.");

        var timer = Stopwatch.StartNew();
        cancellationToken.ThrowIfCancellationRequested();
        string originId = request.OriginMode switch
        {
            SceneOriginMode.SolarSystemBarycenter => "ssb",
            SceneOriginMode.Earth => "earth",
            SceneOriginMode.SimulatedSpacecraft => $"spacecraft:{DemonstrationSpacecraftId}",
            _ => throw new SceneBuildException("Unsupported scientific coordinate origin."),
        };
        var definitions = CelestialObjects
            .Where(d => d.Id != "moon" || request.IncludeMoon)
            .Where(d => _provider.IsBodySupported(d.Id))
            .ToArray();
        var missing = CelestialObjects
            .Where(d => d.Id != "moon" && !_provider.IsBodySupported(d.Id))
            .Select(d => $"{d.Name} (NAIF {d.NaifId}, '{d.Id}')")
            .ToArray();
        if (missing.Length > 0)
            throw new SceneBuildException(
                "The authenticated ephemeris dataset is missing required Solar System states: " + string.Join(", ", missing));

        foreach (var definition in definitions)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!TryGetCoverageEt(definition.Id, out var startEt, out var endEt))
                throw new SceneBuildException($"No ephemeris coverage metadata is available for {definition.Name}.");
            if (request.Epoch.Value < startEt || request.Epoch.Value > endEt)
                throw new SceneBuildException(
                    $"Requested ET {request.Epoch.Value:R}s is outside {definition.Name}'s DE440s coverage " +
                    $"[{startEt:R}, {endEt:R}]s. No extrapolation is performed.");
        }

        CartesianState? spacecraftSsb = CreateDemonstrationSpacecraft(request.Epoch, cancellationToken);
        var sceneObjects = new List<SceneObject>(definitions.Length + 1);
        int trailSamples = 0;
        foreach (var definition in definitions)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var state = QueryState(definition.Id, request, spacecraftSsb);
            if (!state.IsSuccess)
                throw new SceneBuildException(
                    $"{definition.Name} could not be evaluated at ET {request.Epoch.Value:R}s " +
                    $"[{state.Status}]: {state.ErrorMessage}");
            var acceptedState = ApplyTimeQuality(state.Value!, request.TimeQuality);
            IReadOnlyList<TrailSample> trail = Array.Empty<TrailSample>();
            if (request.IncludeTrails && definition.SamplingPeriodDays.HasValue)
            {
                trail = BuildTrail(definition, request, cancellationToken);
                trailSamples += trail.Count;
            }
            sceneObjects.Add(new SceneObject(definition, definition.Id, definition.Name,
                definition.NaifId, definition.CenterClassification, acceptedState, trail));
        }

        var spacecraftSceneState = BuildSpacecraftSceneState(request, spacecraftSsb!);
        sceneObjects.Add(new SceneObject(null, "spacecraft", "SN7 demonstration spacecraft", null,
            DemonstrationSpacecraftDescription, ApplyTimeQuality(spacecraftSceneState, request.TimeQuality),
            Array.Empty<TrailSample>()));

        cancellationToken.ThrowIfCancellationRequested();
        if (trailSamples > MaximumTotalTrailSamples)
            throw new SceneBuildException("Internal trail-sampling bound was exceeded.");
        timer.Stop();
        return new NavigationScene(request.Epoch, request.OriginMode, originId,
            request.Orientation, sceneObjects, request.TimeQuality, request.EpochRepresentation,
            timer.Elapsed, trailSamples, _provider.DataSource);
    }

    private NavigationResult<CartesianState> QueryState(string bodyId, SceneBuildRequest request,
        CartesianState? spacecraftSsb)
    {
        if (request.OriginMode == SceneOriginMode.SimulatedSpacecraft)
            return _framedStates.GetSpacecraftRelative(bodyId, spacecraftSsb!, DemonstrationSpacecraftId);
        string origin = request.OriginMode == SceneOriginMode.Earth ? "earth" : "ssb";
        return _framedStates.GetStateAtEt(bodyId, request.Epoch, request.Orientation, origin);
    }

    private CartesianState CreateDemonstrationSpacecraft(EtSeconds epoch, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var earth = _framedStates.GetStateAtEt("earth", epoch, FrameOrientation.J2000, "ssb");
        if (!earth.IsSuccess)
            throw new SceneBuildException(
                $"The simulated spacecraft needs an Earth reference state, unavailable [{earth.Status}]: {earth.ErrorMessage}");
        var e = earth.Value!;
        return new CartesianState
        {
            Position = new Vector3D(e.Position.X, e.Position.Y + 42_164_000.0, e.Position.Z),
            Velocity = new Vector3D(e.Velocity.X + 3_074.6, e.Velocity.Y, e.Velocity.Z),
            Orientation = FrameOrientation.J2000,
            OriginId = "ssb",
            Epoch = new AstroTime(epoch.ToJulianDateTdb(), TimeScale.Tdb),
            EtSeconds = epoch.Value,
            Provenance = $"Explicit SN7 simulation scenario: {DemonstrationSpacecraftDescription}",
            IsSimulated = true,
            Quality = NavigationResultStatus.ApproximateOrSimulated,
        };
    }

    private CartesianState BuildSpacecraftSceneState(SceneBuildRequest request, CartesianState spacecraftSsb)
    {
        if (request.OriginMode == SceneOriginMode.SimulatedSpacecraft)
            return new CartesianState
            {
                Position = new Vector3D(0, 0, 0),
                Velocity = new Vector3D(0, 0, 0),
                Orientation = FrameOrientation.J2000,
                OriginId = $"spacecraft:{DemonstrationSpacecraftId}",
                Epoch = spacecraftSsb.Epoch,
                EtSeconds = spacecraftSsb.EtSeconds,
                Provenance = spacecraftSsb.Provenance + " | spacecraft-origin reference marker",
                IsSimulated = true,
                Quality = NavigationResultStatus.ApproximateOrSimulated,
            };

        if (request.OriginMode == SceneOriginMode.SolarSystemBarycenter)
            return request.Orientation == FrameOrientation.J2000
                ? spacecraftSsb
                : TransformToIauEarth(spacecraftSsb);

        var earth = _framedStates.GetStateAtEt("earth", request.Epoch, FrameOrientation.J2000, "ssb");
        if (!earth.IsSuccess)
            throw new SceneBuildException($"Earth-relative spacecraft state unavailable: {earth.ErrorMessage}");
        var relative = OriginTranslation.ExpressRelativeTo(spacecraftSsb, earth.Value!, "earth");
        return request.Orientation == FrameOrientation.J2000 ? relative : TransformToIauEarth(relative);
    }

    private CartesianState TransformToIauEarth(CartesianState state)
    {
        if (_orientationModel is null)
            throw new SceneBuildException("Earth-fixed orientation is unavailable because authenticated SN6 data did not load.");
        _orientationModel.GetAuthenticatedRotation(state.EtSeconds, out var r, out var rDot);
        var transformed = StateTransformMath.TransformState(r, rDot, state.Position, state.Velocity);
        return new CartesianState
        {
            Position = transformed.Position,
            Velocity = transformed.Velocity,
            Orientation = FrameOrientation.IauEarth,
            OriginId = state.OriginId,
            Epoch = state.Epoch,
            EtSeconds = state.EtSeconds,
            Provenance = state.Provenance + $" | SN6 J2000->IAU_EARTH at ET={state.EtSeconds:R}s ({_orientationModel.Provenance})",
            IsSimulated = state.IsSimulated,
            Quality = state.Quality,
        };
    }

    private IReadOnlyList<TrailSample> BuildTrail(CelestialObjectDefinition definition,
        SceneBuildRequest request, CancellationToken cancellationToken)
    {
        if (!TryGetCoverageEt(definition.Id, out double bodyStart, out double bodyEnd))
            return Array.Empty<TrailSample>();
        double start = bodyStart;
        double end = bodyEnd;
        if (request.OriginMode == SceneOriginMode.Earth &&
            TryGetCoverageEt("earth", out double earthStart, out double earthEnd))
        {
            start = Math.Max(start, earthStart);
            end = Math.Min(end, earthEnd);
        }
        double periodSeconds = definition.SamplingPeriodDays!.Value * EtSeconds.SecondsPerDay;
        double intervalSeconds = periodSeconds / TrailIntervalsPerOrbit;
        double first = Math.Max(start, request.Epoch.Value - periodSeconds * 0.5);
        double last = Math.Min(end, request.Epoch.Value + periodSeconds * 0.5);
        if (last < first)
            return Array.Empty<TrailSample>();

        var samples = new List<TrailSample>(MaximumTrailSamplesPerObject);
        // Enumerate at a fixed interval and include the selected epoch when it
        // lies on the sampled interval. The loop count is bounded at 49.
        for (int i = 0; i <= TrailIntervalsPerOrbit; i++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            double et = request.Epoch.Value - periodSeconds * 0.5 + i * intervalSeconds;
            if (et < first || et > last)
                continue;
            CartesianState? state = null;
            if (request.OriginMode == SceneOriginMode.SimulatedSpacecraft)
            {
                var simulated = CreateDemonstrationSpacecraft(new EtSeconds(et), cancellationToken);
                var query = _framedStates.GetSpacecraftRelative(definition.Id, simulated, DemonstrationSpacecraftId);
                if (query.IsSuccess)
                    state = ApplyTimeQuality(query.Value!, request.TimeQuality);
            }
            else
            {
                string origin = request.OriginMode == SceneOriginMode.Earth ? "earth" : "ssb";
                var query = _framedStates.GetStateAtEt(definition.Id, new EtSeconds(et), request.Orientation, origin);
                if (query.IsSuccess)
                    state = ApplyTimeQuality(query.Value!, request.TimeQuality);
            }
            if (state is not null)
                samples.Add(new TrailSample(et, state.Position));
        }
        return samples;
    }

    private bool TryGetCoverageEt(string bodyId, out double startEt, out double endEt)
    {
        if (_provider is IndexedChebyshevEphemerisProvider indexed)
        {
            var segment = indexed.Segments.FirstOrDefault(x => x.BodyId == bodyId);
            if (segment is not null)
            {
                startEt = segment.InitEt;
                endEt = segment.EndEt;
                return true;
            }
            if (bodyId == "ssb" && indexed.Segments.Count > 0)
            {
                startEt = indexed.Segments.Min(x => x.InitEt);
                endEt = indexed.Segments.Max(x => x.EndEt);
                return true;
            }
        }
        var coverage = _provider.GetCoverage(bodyId);
        if (coverage.HasValue)
        {
            startEt = EtSeconds.FromJulianDateTdb(coverage.Value.Start.JdUtc).Value;
            endEt = EtSeconds.FromJulianDateTdb(coverage.Value.End.JdUtc).Value;
            return double.IsFinite(startEt) && double.IsFinite(endEt) && endEt >= startEt;
        }
        startEt = endEt = 0;
        return false;
    }

    private static CartesianState ApplyTimeQuality(CartesianState state, TimeConversionQuality? timeQuality)
    {
        if (timeQuality != TimeConversionQuality.ProvisionalStaleTable ||
            state.Quality == NavigationResultStatus.StaleData)
            return state;
        return new CartesianState
        {
            Position = state.Position,
            Velocity = state.Velocity,
            Orientation = state.Orientation,
            OriginId = state.OriginId,
            Epoch = state.Epoch,
            EtSeconds = state.EtSeconds,
            PositionUnit = state.PositionUnit,
            VelocityUnit = state.VelocityUnit,
            Provenance = state.Provenance + " | UTC time quality=ProvisionalStaleTable",
            IsSimulated = state.IsSimulated,
            Quality = NavigationResultStatus.StaleData,
        };
    }
}
