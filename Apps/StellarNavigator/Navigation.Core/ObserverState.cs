using Astronomy.Core;

namespace Navigation.Core;

public sealed class ObserverState
{
    public required string ObserverId { get; init; }
    public required Vector3D Position { get; init; }
    public required ReferenceFrame Frame { get; init; }
    public required string CenterOfReferenceId { get; init; }
    public required AstroTime Epoch { get; init; }
    public StateProvenance Provenance { get; init; } = StateProvenance.Simulated;
    public NavigationResultStatus Quality { get; init; } = NavigationResultStatus.ApproximateOrSimulated;

    public Vector3D DirectionTo(Vector3D targetPosition)
    {
        if (Frame.Kind != ReferenceFrameKind.IcrfJ2000)
            throw new InvalidOperationException("DirectionTo requires ICRF/J2000 frame");
        return (targetPosition - Position).Normalized();
    }

    public double DistanceTo(Vector3D targetPosition) =>
        Position.DistanceTo(targetPosition);

    public override string ToString() =>
        $"Observer {ObserverId} @ {Epoch.ToIsoString()}: pos={Position}, frame={Frame}";
}
