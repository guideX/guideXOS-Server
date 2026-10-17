using System.Text.Json;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 ephemeris integration: the framed service consumes the untouched SN4
/// provider (J2000/SSB geometric states) and produces labeled derived
/// states. SSB-centered geometric states are never confused with
/// Earth-centered ones; source quality and simulated provenance propagate;
/// unsupported frames never fall back; out-of-coverage/unknown-body/non-TDB
/// inputs are typed failures. The Earth-fixed combination is additionally
/// checked against the independent CSPICE rotation at J2000 (et=0, where
/// the managed rotation matches CSPICE exactly).
/// </summary>
public class Sn6EphemerisIntegrationTests
{
    private static readonly AstroTime J2000 = new(2451545.0, TimeScale.Tdb);
    private static readonly AstroTime J2000Plus1000 = new(2452545.0, TimeScale.Tdb);

    private readonly ITestOutputHelper _output;

    public Sn6EphemerisIntegrationTests(ITestOutputHelper output)
    {
        _output = output;
    }

    [Fact]
    public void J2000Ssb_MatchesProviderExactly()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        foreach (var epoch in new[] { J2000, J2000Plus1000 })
        {
            var direct = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
            Assert.True(direct.IsSuccess);
            var framed = svc.GetState("earth", epoch, FrameOrientation.J2000, "ssb");
            Assert.True(framed.IsSuccess, framed.ErrorMessage);
            Assert.Equal(direct.Value!.Position, framed.Value!.Position);
            Assert.Equal(direct.Value!.Velocity, framed.Value!.Velocity);
            Assert.Equal("ssb", framed.Value.OriginId);
            Assert.Equal(NavigationResultStatus.ValidatedReference, framed.Value.Quality);
        }
    }

    [Fact]
    public void MarsRelativeEarth_J2000_MatchesProviderDifference()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var framed = svc.GetState("mars", J2000, FrameOrientation.J2000, "earth");
        Assert.True(framed.IsSuccess, framed.ErrorMessage);
        var mars = provider.GetState("mars", J2000, ReferenceFrame.Barycentric).Value!;
        var earth = provider.GetState("earth", J2000, ReferenceFrame.Barycentric).Value!;
        Assert.True((framed.Value!.Position - (mars.Position - earth.Position)).Magnitude <= 1e-6);
        Assert.True((framed.Value.Velocity - (mars.Velocity - earth.Velocity)).Magnitude <= 1e-9);
        Assert.Equal("earth", framed.Value.OriginId);
        Assert.Equal(FrameOrientation.J2000, framed.Value.Orientation);
    }

    [Fact]
    public void MarsRelativeEarth_EarthFixed_MatchesCspiceRotation()
    {
        // Independent check: expected Earth-fixed state built with the CSPICE
        // pxform/sxform rotation at et=0 from the fixture (managed rotation
        // agrees with CSPICE exactly at J2000, so this tests the service
        // composition, not the rotation itself).
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider, IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath()));
        var framed = svc.GetState("mars", J2000, FrameOrientation.IauEarth, "earth");
        Assert.True(framed.IsSuccess, framed.ErrorMessage);
        var mars = provider.GetState("mars", J2000, ReferenceFrame.Barycentric).Value!;
        var earth = provider.GetState("earth", J2000, ReferenceFrame.Barycentric).Value!;
        var relPos = mars.Position - earth.Position;
        var relVel = mars.Velocity - earth.Velocity;

        var fixture = JsonSerializer.Deserialize<Sn6ReferenceTests.Sn6Fixture>(
            File.ReadAllText(Sn6Paths.Sn6ReferencePath()),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
        var j2000 = fixture.Cases.First(c => c.Tag == "j2000");
        var r = new RotationMatrix(
            j2000.R[0][0], j2000.R[0][1], j2000.R[0][2],
            j2000.R[1][0], j2000.R[1][1], j2000.R[1][2],
            j2000.R[2][0], j2000.R[2][1], j2000.R[2][2]);
        var rd = new RotationMatrix.Derivative(
            j2000.Rdot[0][0], j2000.Rdot[0][1], j2000.Rdot[0][2],
            j2000.Rdot[1][0], j2000.Rdot[1][1], j2000.Rdot[1][2],
            j2000.Rdot[2][0], j2000.Rdot[2][1], j2000.Rdot[2][2]);
        var (ep, ev) = StateTransformMath.TransformState(r, rd, relPos, relVel);
        double posErr = (framed.Value!.Position - ep).Magnitude;
        double velErr = (framed.Value.Velocity - ev).Magnitude;
        _output.WriteLine($"earth-fixed Mars rel Earth @J2000: posErr={posErr:E3} m velErr={velErr:E3} m/s");
        Assert.True(posErr <= 1e-6, $"posErr {posErr:E3}");
        Assert.True(velErr <= 1e-9, $"velErr {velErr:E3}");
        Assert.Equal(FrameOrientation.IauEarth, framed.Value.Orientation);
        Assert.Equal("earth", framed.Value.OriginId);
        Assert.Contains("IAU_EARTH", framed.Value.Provenance);
    }

    [Fact]
    public void SsbAndEarthCentered_AreNotConfused()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var atSsb = svc.GetState("mars", J2000, FrameOrientation.J2000, "ssb");
        var atEarth = svc.GetState("mars", J2000, FrameOrientation.J2000, "earth");
        Assert.True(atSsb.IsSuccess && atEarth.IsSuccess);
        // They differ by Earth's SSB position (~1 AU); labels say which is which.
        double diff = (atSsb.Value!.Position - atEarth.Value!.Position).Magnitude;
        Assert.True(diff > 1e11, $"states suspiciously close: {diff:E3}");
        Assert.Equal("ssb", atSsb.Value.OriginId);
        Assert.Equal("earth", atEarth.Value.OriginId);
    }

    [Fact]
    public void UnsupportedFrameName_NeverFallsBack()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        foreach (var bad in new[] { "ITRF93", "EARTH_FIXED", "GALACTIC", "" })
        {
            var r = svc.GetState("mars", J2000, bad, "ssb");
            Assert.False(r.IsSuccess);
            Assert.Equal(NavigationResultStatus.UnsupportedCalculation, r.Status);
        }
    }

    [Fact]
    public void OutOfCoverage_PropagatesStaleData()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var farFuture = new AstroTime(2510000.0, TimeScale.Tdb);
        var r = svc.GetState("earth", farFuture, FrameOrientation.J2000, "ssb");
        Assert.False(r.IsSuccess);
        Assert.Equal(NavigationResultStatus.StaleData, r.Status);
    }

    [Fact]
    public void UnknownBody_PropagatesInsufficientInformation()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var r = svc.GetState("pluto", J2000, FrameOrientation.J2000, "ssb");
        Assert.False(r.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, r.Status);
    }

    [Fact]
    public void NonTdbEpoch_Rejected_NeverSilentUtcAsEt()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var utcLabel = new AstroTime(2451545.0, TimeScale.Utc);
        var r = svc.GetState("earth", utcLabel, FrameOrientation.J2000, "ssb");
        Assert.False(r.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, r.Status);
    }

    [Fact]
    public void SpacecraftOrigin_RequiresRelativeApi()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var r = svc.GetState("mars", J2000, FrameOrientation.J2000, "spacecraft:sim-1");
        Assert.False(r.IsSuccess);
        Assert.Equal(NavigationResultStatus.UnsupportedCalculation, r.Status);
    }
}
