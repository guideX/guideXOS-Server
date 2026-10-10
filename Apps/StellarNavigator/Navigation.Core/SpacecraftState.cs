using Astronomy.Core;

namespace Navigation.Core;

public enum StateProvenance
{
    Simulated,
    Measured,
    Propagated,
    Estimated
}

public sealed class SpacecraftState
{
    public required string SpacecraftId { get; init; }
    public required Vector3D Position { get; init; }
    public required Vector3D Velocity { get; init; }
    public required ReferenceFrame Frame { get; init; }
    public required string CenterOfReferenceId { get; init; }
    public required AstroTime Epoch { get; init; }
    public required StateProvenance Provenance { get; init; }
    public NavigationResultStatus Quality { get; init; } = NavigationResultStatus.ApproximateOrSimulated;
    public Vector3D? Attitude { get; init; }
    public Vector3D? AngularVelocity { get; init; }

    public double Speed => Velocity.Magnitude;

    public override string ToString() =>
        $"{SpacecraftId} @ {Epoch.ToIsoString()}: pos={Position}, vel={Velocity}, frame={Frame}, provenance={Provenance}, quality={Quality}";
}
