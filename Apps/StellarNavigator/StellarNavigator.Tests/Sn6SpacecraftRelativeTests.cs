using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 spacecraft-relative coordinates over the existing SpacecraftState
/// abstraction, exercised through the framed service: translated-frame
/// labeling, simulated-provenance stickiness, and typed handling of missing
/// states, mismatched epochs/origins/frames, unavailable data, invalid
/// simulated states, and non-finite vectors. Camera coordinates are never
/// consulted: the test constructs a decoy camera position and proves the
/// result is independent of it.
/// </summary>
public class Sn6SpacecraftRelativeTests
{
    private static readonly AstroTime J2000 = new(2451545.0, TimeScale.Tdb);

    private static SpacecraftState SimSc(Vector3D pos, Vector3D vel, string origin = "ssb") =>
        new()
        {
            SpacecraftId = "sim-1",
            Position = pos,
            Velocity = vel,
            Frame = ReferenceFrame.Barycentric,
            CenterOfReferenceId = origin,
            Epoch = J2000,
            Provenance = StateProvenance.Simulated,
            Quality = NavigationResultStatus.ApproximateOrSimulated,
        };

    [Fact]
    public void MarsRelativeToSimulatedSpacecraft_HasPositionAndVelocity()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        // Simulated spacecraft parked at the Earth geocenter with zero velocity.
        var earth = provider.GetState("earth", J2000, ReferenceFrame.Barycentric);
        Assert.True(earth.IsSuccess);
        var (epos, evel) = earth.Value!;
        var sc = SimSc(epos, new Vector3D(0, 0, 0));
        var rel = svc.GetSpacecraftRelative("mars", sc);
        Assert.True(rel.IsSuccess, rel.ErrorMessage);
        var mars = provider.GetState("mars", J2000, ReferenceFrame.Barycentric);
        var (mpos, mvel) = mars.Value!;
        Assert.True((rel.Value!.Position - (mpos - epos)).Magnitude <= 1e-6);
        Assert.True((rel.Value.Velocity - mvel).Magnitude <= 1e-9);
        Assert.Equal("spacecraft:sim-1", rel.Value.OriginId);
        Assert.Equal(FrameOrientation.J2000, rel.Value.Orientation);
        Assert.True(rel.Value.IsSpacecraftRelativeTranslatedFrame);
        // Simulated input stays simulation-derived despite validated transforms.
        Assert.True(rel.Value.IsSimulated);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, rel.Value.Quality);
        Assert.Contains("NOT a body frame", rel.Value.Provenance);
    }

    [Fact]
    public void CameraPosition_IsNeverSpacecraftPosition()
    {
        var sc = SimSc(new Vector3D(1e9, 2e9, 3e9), new Vector3D(1000, 2000, 3000));
        var cameraPosition = new Vector3D(0, 0, 0); // decoy: must not leak in
        var scCart = SpacecraftRelativeState.FromSpacecraftState(sc);
        Assert.Equal(new Vector3D(1e9, 2e9, 3e9), scCart.Position);
        Assert.NotEqual(cameraPosition, scCart.Position);
        Assert.Equal(StateProvenance.Simulated, sc.Provenance);
    }

    [Fact]
    public void MissingSpacecraftState_IsTypedFailure()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var r1 = svc.GetSpacecraftRelative("mars", (SpacecraftState)null!);
        Assert.False(r1.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, r1.Status);
        var r2 = svc.GetSpacecraftRelative("mars", (CartesianState)null!, "x");
        Assert.False(r2.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, r2.Status);
    }

    [Fact]
    public void EpochMismatch_IsTypedFailure()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var sc = SimSc(new Vector3D(0, 0, 0), new Vector3D(0, 0, 0));
        // Spacecraft Cartesian at a different epoch than the service query label.
        // Spacecraft ET disagrees with its own epoch label (the service fetches
        // the target at the label, so the two states genuinely mismatch).
        var scCart = new CartesianState
        {
            Position = new Vector3D(0, 0, 0),
            Velocity = new Vector3D(0, 0, 0),
            Orientation = FrameOrientation.J2000,
            OriginId = "ssb",
            Epoch = J2000,
            EtSeconds = 86400.0,
            Provenance = "test",
            IsSimulated = true,
            Quality = NavigationResultStatus.ApproximateOrSimulated,
        };
        var rel = svc.GetSpacecraftRelative("mars", scCart, "sim-1");
        Assert.False(rel.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, rel.Status);
        Assert.NotNull(sc);
    }

    [Fact]
    public void NonJ2000SpacecraftFrame_Rejected()
    {
        var sc = new SpacecraftState
        {
            SpacecraftId = "sim-1",
            Position = new Vector3D(0, 0, 0),
            Velocity = new Vector3D(0, 0, 0),
            Frame = ReferenceFrame.PlanetCentered("earth"),
            CenterOfReferenceId = "earth",
            Epoch = J2000,
            Provenance = StateProvenance.Simulated,
        };
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var rel = svc.GetSpacecraftRelative("mars", sc);
        Assert.False(rel.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, rel.Status);
    }

    [Fact]
    public void NonFiniteSpacecraftState_Rejected()
    {
        var sc = SimSc(new Vector3D(double.NaN, 0, 0), new Vector3D(0, 0, 0));
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var rel = svc.GetSpacecraftRelative("mars", sc);
        Assert.False(rel.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, rel.Status);
    }

    [Fact]
    public void UnavailableTarget_IsPropagated()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var sc = SimSc(new Vector3D(0, 0, 0), new Vector3D(0, 0, 0));
        var rel = svc.GetSpacecraftRelative("pluto", sc);
        Assert.False(rel.IsSuccess);
    }

    [Fact]
    public void EarthRelativeVelocity_MatchesProviderDifference()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var sc = SimSc(new Vector3D(0, 0, 0), new Vector3D(0, 0, 0));
        var rel = svc.GetSpacecraftRelative("earth", sc);
        Assert.True(rel.IsSuccess, rel.ErrorMessage);
        var earth = provider.GetState("earth", J2000, ReferenceFrame.Barycentric);
        var (epos, evel) = earth.Value!;
        Assert.True((rel.Value!.Position - epos).Magnitude <= 1e-6);
        Assert.True((rel.Value.Velocity - evel).Magnitude <= 1e-9);
    }
}
