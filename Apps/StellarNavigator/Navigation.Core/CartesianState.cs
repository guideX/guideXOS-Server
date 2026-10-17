using Astronomy.Core;

namespace Navigation.Core;

/// <summary>
/// SN6 six-component Cartesian state with explicit reference-frame
/// semantics: axis orientation, coordinate origin, epoch/time scale,
/// provenance, and SI units (m, m/s).
///
/// A frame specifies axis orientation; the origin is carried separately
/// (origin translation is NOT an orientation change). Two states share a
/// directly comparable meaning only when orientation, origin, and epoch
/// all agree.
/// </summary>
public sealed class CartesianState
{
    /// <summary>Position in metres, expressed in <see cref="Orientation"/> axes.</summary>
    public required Vector3D Position { get; init; }
    /// <summary>Velocity in metres/second, consistent with the state transform law.</summary>
    public required Vector3D Velocity { get; init; }
    /// <summary>Axis orientation (J2000 inertial or IAU_EARTH body-fixed).</summary>
    public required FrameOrientation Orientation { get; init; }
    /// <summary>
    /// Coordinate origin id: "ssb", "earth", "sun", or "spacecraft:{id}".
    /// A translated frame (e.g. spacecraft-relative) keeps J2000 axes and
    /// changes only this origin.
    /// </summary>
    public required string OriginId { get; init; }
    /// <summary>Epoch. IAU_EARTH evaluation requires TimeScale.Tdb.</summary>
    public required AstroTime Epoch { get; init; }
    /// <summary>ET seconds past J2000 (TDB) matching <see cref="Epoch"/>.</summary>
    public required double EtSeconds { get; init; }
    /// <summary>Position unit (SI only).</summary>
    public Sn6PositionUnit PositionUnit { get; init; } = Sn6PositionUnit.Metre;
    /// <summary>Velocity unit (SI only).</summary>
    public Sn6VelocityUnit VelocityUnit { get; init; } = Sn6VelocityUnit.MetrePerSecond;
    /// <summary>Where this state came from (ephemeris source, transform chain, simulation).</summary>
    public required string Provenance { get; init; }
    /// <summary>True when derived from a simulated (not measured) spacecraft state.</summary>
    public required bool IsSimulated { get; init; }
    public required NavigationResultStatus Quality { get; init; }

    /// <summary>
    /// True for the SN6 spacecraft-relative translated frame: J2000-aligned
    /// axes with a spacecraft origin. This is NOT a vehicle body frame
    /// (no attitude); it does not rotate because a camera moves.
    /// </summary>
    public bool IsSpacecraftRelativeTranslatedFrame =>
        Orientation == FrameOrientation.J2000 &&
        OriginId.StartsWith("spacecraft:", StringComparison.Ordinal);

    public override string ToString() =>
        $"pos=({Position.X:E6}, {Position.Y:E6}, {Position.Z:E6}) m, " +
        $"vel=({Velocity.X:E6}, {Velocity.Y:E6}, {Velocity.Z:E6}) m/s, " +
        $"orientation={Orientation}, origin={OriginId}, ET={EtSeconds:F3}s [{Quality}]";
}

/// <summary>SN6 validation helpers for Cartesian states.</summary>
public static class CartesianStateValidation
{
    /// <summary>Ensure the stored epoch label and ET metadata describe the same TDB instant.</summary>
    public static void RequireConsistentTdbEpoch(CartesianState state)
    {
        if (state.Epoch.OriginalScale != TimeScale.Tdb)
            throw new ArgumentException(
                $"Cartesian state epoch metadata must be TDB; got {state.Epoch.OriginalScale}.", nameof(state));
        if (!double.IsFinite(state.Epoch.JdUtc))
            throw new ArgumentException("Cartesian state epoch JD must be finite.", nameof(state));
        double labelEt = (state.Epoch.JdUtc - 2451545.0) * 86400.0;
        if (!double.IsFinite(labelEt) || Math.Abs(labelEt - state.EtSeconds) > 1e-3)
            throw new ArgumentException(
                $"Cartesian state ET ({state.EtSeconds:R}s) disagrees with its TDB epoch label ({labelEt:R}s) by more than 1 ms.", nameof(state));
    }

    public static void RequireFinite(CartesianState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!double.IsFinite(state.Position.X) || !double.IsFinite(state.Position.Y) || !double.IsFinite(state.Position.Z))
            throw new ArgumentException("State position contains non-finite components.", nameof(state));
        if (!double.IsFinite(state.Velocity.X) || !double.IsFinite(state.Velocity.Y) || !double.IsFinite(state.Velocity.Z))
            throw new ArgumentException("State velocity contains non-finite components.", nameof(state));
        if (!double.IsFinite(state.EtSeconds))
            throw new ArgumentException("State ET must be finite.", nameof(state));
        if (string.IsNullOrWhiteSpace(state.OriginId))
            throw new ArgumentException("State origin id must be provided.", nameof(state));
    }
}
