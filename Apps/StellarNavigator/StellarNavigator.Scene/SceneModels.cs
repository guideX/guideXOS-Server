using Astronomy.Core;
using Astronomy.Time;
using Navigation.Core;

namespace StellarNavigator.Scene;

public enum SceneOriginMode
{
    SolarSystemBarycenter,
    Earth,
    SimulatedSpacecraft,
}

public enum EpochInputKind
{
    EtSeconds,
    JulianDateTdb,
    Utc,
}

/// <summary>Exact DE440s target identity and its documented center convention.</summary>
public sealed record CelestialObjectDefinition(
    string Id,
    string Name,
    int NaifId,
    string CenterClassification,
    double? SamplingPeriodDays);

public sealed record TrailSample(double EtSeconds, Vector3D PositionMeters);

public sealed record SceneObject(
    CelestialObjectDefinition? Definition,
    string Id,
    string Name,
    int? NaifId,
    string CenterClassification,
    CartesianState State,
    IReadOnlyList<TrailSample> Trail)
{
    public double DistanceFromOriginMeters => State.Position.Magnitude;
    public bool IsSimulated => State.IsSimulated;
    public TrajectoryRenderSample? ModeledSample { get; init; }
    public TrajectoryManeuverMarker? ManeuverSelection { get; init; }
}

public sealed record SceneBuildRequest(
    EtSeconds Epoch,
    SceneOriginMode OriginMode = SceneOriginMode.SolarSystemBarycenter,
    FrameOrientation Orientation = FrameOrientation.J2000,
    bool IncludeMoon = true,
    bool IncludeTrails = false,
    TimeConversionQuality? TimeQuality = null,
    TimeRepresentationPrecision EpochRepresentation = TimeRepresentationPrecision.EtSeconds,
    bool IncludeSimulatedTrajectory = false,
    SimulatedTrajectoryScenario? TrajectoryScenario = null,
    IReadOnlyList<ImpulsiveManeuver>? Maneuvers = null,
    double? TrajectoryWindowHalfSpanSeconds = null,
    int TrajectorySampleCount = 49,
    string TrajectoryAnalysisTargetBodyId = "earth");

public sealed record TrajectoryRenderSample(
    string Id,
    CartesianState State,
    OrbitState SunRelativeState,
    int SegmentIndex,
    TrajectorySampleKind Kind);

public sealed record TrajectoryManeuverMarker(
    string Id,
    string Label,
    double EtSeconds,
    OrbitState PreManeuverSunRelativeState,
    OrbitState PostManeuverSunRelativeState,
    CartesianState PreState,
    CartesianState PostState,
    Vector3D DeltaVelocityMetersPerSecond,
    double MagnitudeMetersPerSecond,
    bool Enabled,
    int SegmentBefore,
    int SegmentAfter);

public sealed record TrajectoryVisualization(
    SimulatedTrajectory Model,
    SceneObject VehicleMarker,
    IReadOnlyList<TrajectoryRenderSample> Samples,
    IReadOnlyList<TrajectoryManeuverMarker> ManeuverMarkers,
    TimeSpan CompositionDuration)
{
    public MissionEventTimeline? MissionEvents { get; init; }
}

public sealed record NavigationScene(
    EtSeconds Epoch,
    SceneOriginMode OriginMode,
    string ScientificOriginId,
    FrameOrientation Orientation,
    IReadOnlyList<SceneObject> Objects,
    TimeConversionQuality? TimeQuality,
    TimeRepresentationPrecision EpochRepresentation,
    TimeSpan BuildDuration,
    int TrailSampleCount,
    string DataSource)
{
    public TrajectoryVisualization? Trajectory { get; init; }

    public SceneObject? FindObject(string id)
    {
        var existing = Objects.FirstOrDefault(x => x.Id == id);
        if (existing is not null)
            return existing;
        if (Trajectory is null)
            return null;
        if (Trajectory.VehicleMarker.Id == id)
            return Trajectory.VehicleMarker;
        var sample = Trajectory.Samples.FirstOrDefault(x => x.Id == id);
        if (sample is not null)
            return new SceneObject(null, sample.Id, "SN8 trajectory sample", null,
                "Sampled state from an idealized Sun-centered two-body trajectory", sample.State,
                Array.Empty<TrailSample>()) { ModeledSample = sample };
        var marker = Trajectory.ManeuverMarkers.FirstOrDefault(x => $"maneuver:{x.Id}" == id);
        if (marker is null)
            return null;
        string name = string.IsNullOrWhiteSpace(marker.Label) ? marker.Id : marker.Label;
        if (!marker.Enabled)
            name += " (disabled)";
        return new SceneObject(null, id, $"Simulated maneuver {name}", null,
            "Instantaneous simulated Δv in J2000 axes; not a thruster command", marker.PostState,
            Array.Empty<TrailSample>()) { ManeuverSelection = marker };
    }
}

public sealed record EpochResolution(
    bool Success,
    EtSeconds Epoch,
    EpochInputKind InputKind,
    TimeConversionQuality? TimeQuality,
    TimeRepresentationPrecision RepresentationPrecision,
    string Provenance,
    string? Error)
{
    public static EpochResolution Failed(EpochInputKind kind, string message,
        TimeConversionQuality? quality = null) =>
        new(false, default, kind, quality, TimeRepresentationPrecision.EtSeconds,
            "Time input not accepted", message);
}

/// <summary>Monotonic ticket gate for discarding superseded asynchronous scene builds.</summary>
public sealed class SceneGenerationGate
{
    private long _generation;

    public long BeginRequest() => Interlocked.Increment(ref _generation);
    public bool IsCurrent(long requestId) => requestId == Volatile.Read(ref _generation);
    public void CancelPending() => Interlocked.Increment(ref _generation);
}
