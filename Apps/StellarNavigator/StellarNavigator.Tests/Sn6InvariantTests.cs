using Astronomy.Core;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 mathematical invariant tests (no CSPICE fixture needed): identity,
/// orthogonality, determinant, inverse law, position-vs-state distinction,
/// rotating-frame velocity coupling, composition, round trips, relative-state
/// identities, and typed rejections. These prove the mathematics is
/// self-consistent; <see cref="Sn6ReferenceTests"/> proves it matches CSPICE.
/// </summary>
public class Sn6InvariantTests
{
    private static readonly IauEarthOrientation Orientation =
        IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
    private static readonly double[] Ets = { 0.0, 86400.0, 100000000.0, -1500000000.0, 800000000.0 };

    private static Vector3D V(double x, double y, double z) => new(x, y, z);

    [Fact]
    public void Identity_RotationAndState()
    {
        var r = RotationMatrix.Identity;
        var zero = new RotationMatrix.Derivative(0, 0, 0, 0, 0, 0, 0, 0, 0);
        var pos = V(1.5e11, -2e10, 5e9);
        var vel = V(30000, 10000, 5000);
        Assert.Equal(pos, r.RotatePosition(pos));
        var (p, v) = StateTransformMath.TransformState(r, zero, pos, vel);
        Assert.Equal(pos, p);
        Assert.Equal(vel, v);
        Assert.Equal(0.0, r.OrthogonalityError());
        Assert.Equal(1.0, r.Determinant());
    }

    [Fact]
    public void Rotation_IsOrthogonal_WithDeterminantPlusOne()
    {
        foreach (var et in Ets)
        {
            Orientation.GetRotation(et, out var r, out _);
            Assert.True(r.OrthogonalityError() < 5e-15, $"et={et}: orth={r.OrthogonalityError():E3}");
            Assert.True(Math.Abs(r.Determinant() - 1.0) < 1e-12, $"et={et}: det={r.Determinant():R}");
        }
    }

    [Fact]
    public void Transpose_IsCorrectInverse_ForPositions()
    {
        var pos = V(1e9, -2e9, 3e8);
        foreach (var et in Ets)
        {
            Orientation.GetRotation(et, out var r, out _);
            var fwd = r.RotatePosition(pos);
            var back = r.Transpose().RotatePosition(fwd);
            Assert.True((back - pos).Magnitude <= 1e-6 * Math.Max(1.0, pos.Magnitude),
                $"et={et}: {(back - pos).Magnitude:E3}");
        }
    }

    [Fact]
    public void Transposing6x6_IsNotTheInverse()
    {
        // Guard against the specific error the spec forbids: the inverse
        // state transform must include velocity coupling, not a whole-matrix
        // transpose. A rotation-only "velocity transform" misses R'*r.
        Orientation.GetRotation(86400.0, out var r, out var rd);
        var pos = V(6.3781366e6, 0, 0); // off-axis: coupling is large here
        var vel = V(0, 0, 0);
        var rotatedOnly = r.RotatePosition(vel); // what a rotation-only mistake yields
        var (_, full) = StateTransformMath.TransformState(r, rd, pos, vel);
        var coupling = rd.Multiply(pos);
        Assert.True(coupling.Magnitude > 400.0, $"coupling unexpectedly small: {coupling.Magnitude}");
        Assert.Equal(0.0, rotatedOnly.Magnitude);
        Assert.True((full - rotatedOnly).Magnitude > 400.0);
    }

    [Fact]
    public void InertiallyStationaryOffAxisPoint_HasNonzeroRotatingVelocity()
    {
        // Earth-surface-scale point, inertially at rest: in IAU_EARTH axes it
        // must stream past at ~465 m/s (sidereal surface speed), proving the
        // velocity law is not rotation-only.
        Orientation.GetRotation(0.0, out var r, out var rd);
        var pos = V(6.3781366e6, 0, 0);
        var (_, v) = StateTransformMath.TransformState(r, rd, pos, V(0, 0, 0));
        Assert.True(v.Magnitude > 464.0 && v.Magnitude < 466.0, $"surface speed: {v.Magnitude}");
    }

    [Fact]
    public void Composition_MatchesSequentialApplication()
    {
        Orientation.GetRotation(100000.0, out var r1, out var d1);
        Orientation.GetRotation(200000.0, out var r2, out var d2);
        var (rc, dc) = StateTransformMath.Compose(r1, d1, r2, d2);
        var pos = V(1e9, 2e9, -3e8);
        var vel = V(1000, -2000, 500);
        var (p1, v1) = StateTransformMath.TransformState(r1, d1, pos, vel);
        var (p2, v2) = StateTransformMath.TransformState(r2, d2, p1, v1);
        var (pc, vc) = StateTransformMath.TransformState(rc, dc, pos, vel);
        Assert.True((pc - p2).Magnitude <= 1e-6, $"composed pos: {(pc - p2).Magnitude:E3}");
        Assert.True((vc - v2).Magnitude <= 1e-6, $"composed vel: {(vc - v2).Magnitude:E3}");
    }

    [Fact]
    public void ForwardThenInverse_RecoversState()
    {
        var pos = V(1.5e11, 2e10, 5e9);
        var vel = V(30000, 10000, 5000);
        foreach (var et in Ets)
        {
            Orientation.GetRotation(et, out var r, out var rd);
            var (p1, v1) = StateTransformMath.TransformState(r, rd, pos, vel);
            var (p2, v2) = StateTransformMath.InverseTransformState(r, rd, p1, v1);
            Assert.True((p2 - pos).Magnitude <= 1e-9 * pos.Magnitude + 1e-6, $"et={et}");
            Assert.True((v2 - vel).Magnitude <= 1e-9 * vel.Magnitude + 1e-9, $"et={et}");
        }
    }

    private static CartesianState CState(Vector3D p, Vector3D v, string origin, double et) =>
        new()
        {
            Position = p,
            Velocity = v,
            Orientation = FrameOrientation.J2000,
            OriginId = origin,
            Epoch = new AstroTime(2451545.0 + et / 86400.0, TimeScale.Tdb),
            EtSeconds = et,
            Provenance = "test",
            IsSimulated = false,
            Quality = NavigationResultStatus.ValidatedReference,
        };

    [Fact]
    public void IdenticalSpacecraftAndTarget_GiveZeroRelative()
    {
        var sc = CState(V(1e9, 2e9, 3e9), V(1000, 2000, 3000), "ssb", 0.0);
        var rel = SpacecraftRelativeState.ComputeRelative(sc, CState(sc.Position, sc.Velocity, "ssb", 0.0), "sc1");
        Assert.Equal(0.0, rel.Position.Magnitude);
        Assert.Equal(0.0, rel.Velocity.Magnitude);
        Assert.Equal("spacecraft:sc1", rel.OriginId);
        Assert.True(rel.IsSpacecraftRelativeTranslatedFrame);
    }

    [Fact]
    public void RelativeVelocity_IsTargetMinusSpacecraft()
    {
        var sc = CState(V(0, 0, 0), V(1000, 0, 0), "ssb", 0.0);
        var tgt = CState(V(1e9, 0, 0), V(4000, 500, 0), "ssb", 0.0);
        var rel = SpacecraftRelativeState.ComputeRelative(sc, tgt, "sc1");
        Assert.Equal(V(1e9, 0, 0), rel.Position);
        Assert.Equal(V(3000, 500, 0), rel.Velocity);
    }

    [Fact]
    public void FrameMismatch_Rejected()
    {
        var sc = CState(V(0, 0, 0), V(0, 0, 0), "ssb", 0.0);
        var bad = new CartesianState
        {
            Position = V(1, 2, 3),
            Velocity = V(0, 0, 0),
            Orientation = FrameOrientation.IauEarth,
            OriginId = "ssb",
            Epoch = sc.Epoch,
            EtSeconds = 0.0,
            Provenance = "test",
            IsSimulated = false,
            Quality = NavigationResultStatus.ValidatedReference,
        };
        Assert.Throws<InvalidOperationException>(() => SpacecraftRelativeState.ComputeRelative(sc, bad, "sc1"));
    }

    [Fact]
    public void OriginMismatch_Rejected()
    {
        var sc = CState(V(0, 0, 0), V(0, 0, 0), "ssb", 0.0);
        var tgt = CState(V(1, 2, 3), V(0, 0, 0), "earth", 0.0);
        Assert.Throws<InvalidOperationException>(() => SpacecraftRelativeState.ComputeRelative(sc, tgt, "sc1"));
    }

    [Fact]
    public void EpochMismatch_Rejected()
    {
        var sc = CState(V(0, 0, 0), V(0, 0, 0), "ssb", 0.0);
        var tgt = CState(V(1, 2, 3), V(0, 0, 0), "ssb", 3600.0);
        Assert.Throws<InvalidOperationException>(() => SpacecraftRelativeState.ComputeRelative(sc, tgt, "sc1"));
    }

    [Fact]
    public void NonFiniteInputs_Rejected()
    {
        var sc = CState(V(0, 0, 0), V(0, 0, 0), "ssb", 0.0);
        var bad = CState(V(double.NaN, 0, 0), V(0, 0, 0), "ssb", 0.0);
        Assert.Throws<ArgumentException>(() => SpacecraftRelativeState.ComputeRelative(sc, bad, "sc1"));
        Assert.Throws<ArgumentException>(() => SpacecraftRelativeState.ComputeRelative(bad, sc, "sc1"));
    }

    [Fact]
    public void Registry_AcceptsOnlySupportedFrames()
    {
        Assert.True(FrameRegistry.TryParseOrientation("J2000", out var o1) && o1 == FrameOrientation.J2000);
        Assert.True(FrameRegistry.TryParseOrientation("IAU_EARTH", out var o2) && o2 == FrameOrientation.IauEarth);
        Assert.False(FrameRegistry.TryParseOrientation("ITRF93", out _));
        Assert.False(FrameRegistry.TryParseOrientation("EARTH_FIXED", out _));
        Assert.False(FrameRegistry.TryParseOrientation("GALACTIC", out _));
        Assert.False(FrameRegistry.TryParseOrientation("", out _));
        Assert.False(FrameRegistry.TryParseOrientation(null, out _));
        Assert.Contains("NOT equivalent", FrameRegistry.UnsupportedDiagnostic("ITRF93"));
    }

    [Fact]
    public void SimulatedBodyFrame_IsClearlyMarked()
    {
        var att = Navigation.Core.SimulatedBodyFrame.Identity(0.0,
            new AstroTime(2451545.0, TimeScale.Tdb), "demo");
        Assert.True(att.IsSimulationOnly);
        Assert.Contains("SIMULATION-ONLY", att.Validity);
        Assert.Contains("SIMULATION-ONLY", Navigation.Core.SimulatedBodyFrame.SimulationMarker);
    }
}
