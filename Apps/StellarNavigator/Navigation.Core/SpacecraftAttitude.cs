using Astronomy.Core;

namespace Navigation.Core;

/// <summary>
/// SN6 spacecraft-attitude boundary (minimal interface for future work).
/// Actual attitude determination is OUT OF SCOPE for SN6: no implementation
/// here fabricates attitude or angular velocity. A future body frame would
/// need an orientation (quaternion or matrix), angular velocity, epoch,
/// source, and validity/uncertainty metadata — all represented below so
/// later phases can plug in real estimators without renaming SN6 concepts.
/// </summary>
public sealed class SpacecraftAttitudeState
{
    /// <summary>Body-to-J2000 orientation at <see cref="Epoch"/>.</summary>
    public required RotationMatrix BodyToJ2000 { get; init; }
    /// <summary>Angular velocity of the body frame (rad/s, body axes).</summary>
    public required Vector3D AngularVelocity { get; init; }
    /// <summary>Epoch of the attitude estimate.</summary>
    public required AstroTime Epoch { get; init; }
    public required double EtSeconds { get; init; }
    /// <summary>Attitude source (estimator name, star tracker id, ...).</summary>
    public required string Source { get; init; }
    /// <summary>Validity/uncertainty note (e.g. covariance trace, age).</summary>
    public required string Validity { get; init; }
    /// <summary>True for placeholders; real estimators report false.</summary>
    public required bool IsSimulationOnly { get; init; }
}

/// <summary>Future attitude estimator contract (no SN6 implementation).</summary>
public interface ISpacecraftAttitudeProvider
{
    bool TryGetAttitude(string spacecraftId, double etSeconds,
        out SpacecraftAttitudeState? attitude, out string? error);
}

/// <summary>
/// SIMULATION-ONLY synthetic body-frame demonstration aid. Clearly labeled;
/// never part of the validated reference path. Do not mistake its output
/// for a determined attitude, and do not represent the translated-J2000
/// frame as a body frame.
/// </summary>
public static class SimulatedBodyFrame
{
    public const string SimulationMarker =
        "SIMULATION-ONLY synthetic attitude (identity orientation, zero rate); NOT a determination.";

    public static SpacecraftAttitudeState Identity(double etSeconds, AstroTime epoch, string spacecraftId) =>
        new()
        {
            BodyToJ2000 = RotationMatrix.Identity,
            AngularVelocity = new Vector3D(0, 0, 0),
            Epoch = epoch,
            EtSeconds = etSeconds,
            Source = $"synthetic identity for {spacecraftId}",
            Validity = SimulationMarker,
            IsSimulationOnly = true,
        };
}
