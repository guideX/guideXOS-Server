using System.Text.Json;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class JplEphemerisTests : IDisposable
{
    private readonly string _tempDir;
    private readonly string _ephemerisPath;

    public JplEphemerisTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sn2_tests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
        var srcPath = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "Astronomy.Ephemeris", "Data", "ephemeris_de440s.json");
        if (!File.Exists(srcPath))
            srcPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s.json");
        _ephemerisPath = Path.Combine(_tempDir, "ephemeris_de440s.json");
        File.Copy(srcPath, _ephemerisPath);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    [Fact]
    public void Dataset_LoadsSuccessfully()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        Assert.NotNull(provider);
        Assert.Contains("DE440s", provider.DataSource);
    }

    [Fact]
    public void Dataset_AllBodiesSupported()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        Assert.True(provider.IsBodySupported("earth"));
        Assert.True(provider.IsBodySupported("moon"));
        Assert.True(provider.IsBodySupported("mars"));
        Assert.True(provider.IsBodySupported("jupiter"));
        Assert.True(provider.IsBodySupported("sun"));
        Assert.True(provider.IsBodySupported("ssb"));
    }

    [Fact]
    public void Dataset_UnknownBody_ReturnsFailure()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("pluto", epoch, ReferenceFrame.Barycentric);
        Assert.False(result.IsSuccess);
    }

    [Fact]
    public void Dataset_OutsideCoverage_ReturnsFailure()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJ2000Days(2000);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.False(result.IsSuccess);
    }

    [Fact]
    public void Dataset_UnsupportedFrame_ReturnsFailure()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);
        Assert.False(result.IsSuccess);
    }

    [Fact]
    public void Dataset_DeterministicRepeatedEvaluation()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJ2000Days(0);
        var r1 = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var r2 = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.Equal(r1.Value!.Position.X, r2.Value!.Position.X, 10);
        Assert.Equal(r1.Value.Position.Y, r2.Value.Position.Y, 10);
        Assert.Equal(r1.Value.Position.Z, r2.Value.Position.Z, 10);
    }

    [Fact]
    public void Dataset_EarthDistance_FromJplReference()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);
        var (pos, _) = result.Value!;
        var distAu = pos.Magnitude / Distance.MetersPerAstronomicalUnit;
        Assert.True(distAu is > 0.98 and < 1.02, $"Earth distance {distAu} AU out of expected range");
    }

    [Fact]
    public void Dataset_MoonDistance_FromJplReference()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var moon = provider.GetState("moon", epoch, ReferenceFrame.Barycentric);
        Assert.True(earth.IsSuccess && moon.IsSuccess);
        var (earthPos, _) = earth.Value!;
        var (moonPos, _) = moon.Value!;
        var distKm = earthPos.DistanceTo(moonPos) / Distance.MetersPerKilometer;
        Assert.True(distKm is > 350000 and < 410000, $"Earth-Moon distance {distKm} km out of expected range");
    }

    [Fact]
    public void Dataset_Coverage_IsReasonable()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var coverage = provider.GetCoverage("earth");
        Assert.NotNull(coverage);
        var spanDays = coverage!.Value.End.JdUtc - coverage.Value.Start.JdUtc;
        Assert.True(spanDays > 100000, $"Coverage span {spanDays} days is too short");
    }

    [Fact]
    public void Dataset_Sha256_IsValid()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        Assert.False(string.IsNullOrEmpty(provider.Sha256));
        Assert.Equal(64, provider.Sha256.Length);
    }

    [Fact]
    public void Dataset_Ssb_IsAtOrigin()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("ssb", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);
        var (pos, vel) = result.Value!;
        Assert.Equal(0, pos.Magnitude, 6);
        Assert.Equal(0, vel.Magnitude, 9);
    }
}
