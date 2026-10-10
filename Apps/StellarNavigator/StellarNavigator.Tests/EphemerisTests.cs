using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class EphemerisTests : IDisposable
{
    private readonly string _tempDir;

    public EphemerisTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sn1_tests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    private PrecomputedEphemerisProvider CreateProvider()
    {
        var path = Path.Combine(_tempDir, "eph.json");
        File.WriteAllText(path, """
        {
          "entries": [
            { "bodyId": "earth", "jd": 2451545.0,
              "position": { "x": -0.177, "y": 0.967, "z": -0.0001 },
              "velocity": { "x": -0.0172, "y": -0.0031, "z": 0.0000 } },
            { "bodyId": "mars", "jd": 2451545.0,
              "position": { "x": 1.382, "y": 0.025, "z": -0.028 },
              "velocity": { "x": -0.0006, "y": 0.0128, "z": 0.0004 } }
          ]
        }
        """);
        return new PrecomputedEphemerisProvider(path);
    }

    [Fact]
    public void GetState_ReturnsPositionAndVelocity()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);

        Assert.True(result.IsSuccess);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, result.Status);
        Assert.NotNull(result.Value);
        var (pos, vel) = result.Value!;
        Assert.True(pos.Magnitude > 0);
        Assert.True(vel.Magnitude > 0);
    }

    [Fact]
    public void GetState_UnknownBody_ReturnsFailure()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("pluto", epoch, ReferenceFrame.Heliocentric);

        Assert.False(result.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, result.Status);
    }

    [Fact]
    public void GetState_OutsideCoverage_ReturnsStaleData()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(400);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);

        Assert.False(result.IsSuccess);
        Assert.Equal(NavigationResultStatus.StaleData, result.Status);
    }

    [Fact]
    public void GetState_UnsupportedFrame_ReturnsUnsupported()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.IcrfJ2000);

        Assert.False(result.IsSuccess);
        Assert.Equal(NavigationResultStatus.UnsupportedCalculation, result.Status);
    }

    [Fact]
    public void IsBodySupported_KnownAndUnknown()
    {
        var provider = CreateProvider();
        Assert.True(provider.IsBodySupported("earth"));
        Assert.False(provider.IsBodySupported("pluto"));
    }

    [Fact]
    public void Coverage_ReturnsRangeForSupportedBody()
    {
        var provider = CreateProvider();
        var coverage = provider.GetCoverage("earth");
        Assert.NotNull(coverage);
        Assert.True(coverage!.Value.Start.JdUtc < coverage.Value.End.JdUtc);
    }

    [Fact]
    public void Coverage_NullForUnsupportedBody()
    {
        var provider = CreateProvider();
        Assert.Null(provider.GetCoverage("pluto"));
    }

    [Fact]
    public void Position_IsInMeters()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);
        var (pos, _) = result.Value!;
        var distAu = pos.Magnitude / Distance.MetersPerAstronomicalUnit;
        Assert.True(distAu is > 0.9 and < 1.1, $"Expected ~1 AU, got {distAu} AU");
    }
}
