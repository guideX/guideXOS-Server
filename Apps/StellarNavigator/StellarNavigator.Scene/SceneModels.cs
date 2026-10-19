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
}

public sealed record SceneBuildRequest(
    EtSeconds Epoch,
    SceneOriginMode OriginMode = SceneOriginMode.SolarSystemBarycenter,
    FrameOrientation Orientation = FrameOrientation.J2000,
    bool IncludeMoon = true,
    bool IncludeTrails = false,
    TimeConversionQuality? TimeQuality = null,
    TimeRepresentationPrecision EpochRepresentation = TimeRepresentationPrecision.EtSeconds);

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
    public SceneObject? FindObject(string id) => Objects.FirstOrDefault(x => x.Id == id);
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
