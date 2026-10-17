using Astronomy.Core;

namespace Navigation.Core;

/// <summary>
/// SN6 origin translation: expressing the SAME physical state relative to
/// another coordinate origin, without changing axis orientation.
///
/// For a target and an origin body in the same orientation, epoch, and
/// units: relativePosition = targetPosition - originPosition,
/// relativeVelocity = targetVelocity - originVelocity.
/// This operation never rotates axes; frame rotation is the separate
/// <see cref="StateTransformMath"/> path. A composed "translate + rotate"
/// query is provided only as an explicitly named two-step operation by the
/// ephemeris service layer.
/// </summary>
public static class OriginTranslation
{
    /// <summary>Epoch agreement tolerance (seconds of ET).</summary>
    public const double EpochToleranceSeconds = 1e-6;

    /// <summary>
    /// Express <paramref name="target"/> relative to <paramref name="originBody"/>.
    /// Both must share orientation, epoch (within tolerance), and SI units.
    /// Result origin is <paramref name="newOriginId"/>; orientation unchanged.
    /// </summary>
    public static CartesianState ExpressRelativeTo(
        CartesianState target, CartesianState originBody, string newOriginId)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(originBody);
        ArgumentException.ThrowIfNullOrWhiteSpace(newOriginId);
        CartesianStateValidation.RequireFinite(target);
        CartesianStateValidation.RequireFinite(originBody);
        CartesianStateValidation.RequireConsistentTdbEpoch(target);
        CartesianStateValidation.RequireConsistentTdbEpoch(originBody);

        if (!string.Equals(target.OriginId, originBody.OriginId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Origin translation requires matching source coordinate origins: '{target.OriginId}' vs '{originBody.OriginId}'. " +
                "Translate both states from their shared original origin; the requested result origin does not change this requirement.");

        if (target.Orientation != originBody.Orientation)
            throw new InvalidOperationException(
                $"Origin translation requires matching orientations: {target.Orientation} vs {originBody.Orientation}. " +
                "Rotate frames first via an explicit state transformation.");
        if (!Enum.IsDefined(target.Orientation))
            throw new InvalidOperationException($"Origin translation received unsupported orientation metadata: {target.Orientation}.");
        if (Math.Abs(target.EtSeconds - originBody.EtSeconds) > EpochToleranceSeconds)
            throw new InvalidOperationException(
                $"Origin translation requires matching epochs (|dET|={Math.Abs(target.EtSeconds - originBody.EtSeconds):E2}s).");
        if (target.PositionUnit != originBody.PositionUnit || target.VelocityUnit != originBody.VelocityUnit ||
            target.PositionUnit != Sn6PositionUnit.Metre || target.VelocityUnit != Sn6VelocityUnit.MetrePerSecond)
            throw new InvalidOperationException(
                $"Origin translation requires matching SI units (m, m/s); got target=({target.PositionUnit}, {target.VelocityUnit}) " +
                $"and origin=({originBody.PositionUnit}, {originBody.VelocityUnit}).");

        bool simulated = target.IsSimulated || originBody.IsSimulated;
        var quality = ComposeQuality(target, originBody);

        return new CartesianState
        {
            Position = target.Position - originBody.Position,
            Velocity = target.Velocity - originBody.Velocity,
            Orientation = target.Orientation,
            OriginId = newOriginId,
            Epoch = target.Epoch,
            EtSeconds = target.EtSeconds,
            Provenance = $"origin-translated {target.OriginId}->{newOriginId} | target({target.Provenance}) origin({originBody.Provenance})",
            IsSimulated = simulated,
            Quality = quality,
        };
    }

    /// <summary>
    /// Conservative state-quality precedence: unsuccessful statuses are
    /// rejected; StaleData remains stale; otherwise simulated or approximate
    /// inputs remain approximate/simulated; only two validated inputs remain
    /// validated. IsSimulated is propagated independently, including when
    /// StaleData takes status precedence.
    /// </summary>
    internal static NavigationResultStatus ComposeQuality(CartesianState target, CartesianState origin)
    {
        static bool IsFailure(NavigationResultStatus q) => q is
            NavigationResultStatus.InsufficientInformation or
            NavigationResultStatus.UnsupportedCalculation or
            NavigationResultStatus.InvalidInput;

        if (IsFailure(target.Quality) || IsFailure(origin.Quality))
            throw new InvalidOperationException(
                $"Origin translation cannot consume unsuccessful state quality: target={target.Quality}, origin={origin.Quality}.");
        if (target.Quality == NavigationResultStatus.StaleData || origin.Quality == NavigationResultStatus.StaleData)
            return NavigationResultStatus.StaleData;
        if (target.IsSimulated || origin.IsSimulated ||
            target.Quality == NavigationResultStatus.ApproximateOrSimulated ||
            origin.Quality == NavigationResultStatus.ApproximateOrSimulated)
            return NavigationResultStatus.ApproximateOrSimulated;
        if (target.Quality == NavigationResultStatus.ValidatedReference &&
            origin.Quality == NavigationResultStatus.ValidatedReference)
            return NavigationResultStatus.ValidatedReference;
        throw new InvalidOperationException(
            $"Origin translation received unsupported state quality: target={target.Quality}, origin={origin.Quality}.");
    }
}

/// <summary>
/// SN6 spacecraft-relative coordinate model over the existing
/// <see cref="SpacecraftState"/> abstraction. The spacecraft and target
/// must share inertial (J2000) orientation, origin, and epoch; the result
/// lives in the spacecraft-relative translated frame (J2000-aligned axes,
/// spacecraft origin). Simulated spacecraft states yield explicitly
/// simulation-derived results; camera coordinates are never consulted and
/// never inferred to be spacecraft position.
/// </summary>
public static class SpacecraftRelativeState
{
    public static CartesianState FromSpacecraftState(SpacecraftState sc)
    {
        ArgumentNullException.ThrowIfNull(sc);
        double et = sc.Epoch.OriginalScale == TimeScale.Tdb
            ? (sc.Epoch.JdUtc - 2451545.0) * 86400.0
            : throw new InvalidOperationException(
                "Spacecraft-relative computation requires a TDB epoch on the spacecraft state; " +
                "convert UTC via the SN5 TimeConverter first.");
        return new CartesianState
        {
            Position = sc.Position,
            Velocity = sc.Velocity,
            Orientation = MapOrientation(sc.Frame),
            OriginId = sc.CenterOfReferenceId,
            Epoch = sc.Epoch,
            EtSeconds = et,
            Provenance = $"spacecraft {sc.SpacecraftId} ({sc.Provenance})",
            IsSimulated = sc.Provenance != StateProvenance.Measured,
            Quality = sc.Quality,
        };
    }

    private static FrameOrientation MapOrientation(ReferenceFrame frame) =>
        frame.Kind switch
        {
            ReferenceFrameKind.IcrfJ2000 or ReferenceFrameKind.Barycentric or ReferenceFrameKind.Heliocentric
                => FrameOrientation.J2000,
            _ => throw new InvalidOperationException(
                $"Spacecraft state frame {frame} is not a J2000-aligned inertial frame. " +
                "Spacecraft-relative translation requires J2000-aligned axes."),
        };

    /// <summary>
    /// Target relative to spacecraft: relativePosition = target - spacecraft,
    /// relativeVelocity likewise. Both inputs must be J2000, same origin and
    /// epoch. Missing (null) spacecraft state, mismatched frames/origins/
    /// epochs, unavailable data, and non-finite vectors are typed failures.
    /// </summary>
    public static CartesianState ComputeRelative(
        CartesianState spacecraft, CartesianState target, string spacecraftId)
    {
        ArgumentNullException.ThrowIfNull(spacecraft);
        ArgumentNullException.ThrowIfNull(target);
        ArgumentException.ThrowIfNullOrWhiteSpace(spacecraftId);
        CartesianStateValidation.RequireFinite(spacecraft);
        CartesianStateValidation.RequireFinite(target);

        if (spacecraft.Orientation != FrameOrientation.J2000 || target.Orientation != FrameOrientation.J2000)
            throw new InvalidOperationException(
                $"Spacecraft-relative computation requires J2000 orientations: " +
                $"spacecraft={spacecraft.Orientation}, target={target.Orientation}.");
        if (!string.Equals(spacecraft.OriginId, target.OriginId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Spacecraft-relative computation requires matching origins: {spacecraft.OriginId} vs {target.OriginId}.");
        if (Math.Abs(spacecraft.EtSeconds - target.EtSeconds) > OriginTranslation.EpochToleranceSeconds)
            throw new InvalidOperationException("Spacecraft-relative computation requires matching epochs.");

        var rel = OriginTranslation.ExpressRelativeTo(target, spacecraft, $"spacecraft:{spacecraftId}");
        // Simulation provenance is sticky: a simulated spacecraft contribution
        // keeps the relative result simulation-derived even though the frame
        // mathematics itself is independently validated.
        bool simulated = spacecraft.IsSimulated || target.IsSimulated;
        var quality = OriginTranslation.ComposeQuality(target, spacecraft);
        return new CartesianState
        {
            Position = rel.Position,
            Velocity = rel.Velocity,
            Orientation = rel.Orientation,
            OriginId = rel.OriginId,
            Epoch = rel.Epoch,
            EtSeconds = rel.EtSeconds,
            Provenance = $"spacecraft-relative to {spacecraftId} (J2000-aligned translated frame, NOT a body frame) | {rel.Provenance}",
            IsSimulated = simulated,
            Quality = quality,
        };
    }
}
