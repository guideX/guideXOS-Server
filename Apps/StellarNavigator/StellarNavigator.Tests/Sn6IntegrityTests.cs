using Astronomy.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 source-data integrity: the compact orientation asset must load,
/// match the managed constants exactly, and any tampering, corruption, or
/// absence must be a typed load failure — never a silent constant drift.
/// </summary>
public class Sn6IntegrityTests
{
    [Fact]
    public void OrientationAsset_Loads()
    {
        var model = IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        Assert.NotNull(model);
        Assert.Contains("pck00011.tpc", model.Provenance);
    }

    [Fact]
    public void Constants_MatchAuthenticatedPck()
    {
        Assert.Equal(0.0, IauEarthOrientation.PoleRa0Deg);
        Assert.Equal(-0.641, IauEarthOrientation.PoleRa1DegPerCentury);
        Assert.Equal(0.0, IauEarthOrientation.PoleRa2DegPerCentury2);
        Assert.Equal(90.0, IauEarthOrientation.PoleDec0Deg);
        Assert.Equal(-0.557, IauEarthOrientation.PoleDec1DegPerCentury);
        Assert.Equal(190.147, IauEarthOrientation.Pm0Deg);
        Assert.Equal(360.9856235, IauEarthOrientation.Pm1DegPerDay);
        Assert.Equal("3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1",
            IauEarthOrientation.SourceKernelSha256);
    }

    [Fact]
    public void MissingAsset_Rejected()
    {
        Assert.Throws<InvalidDataException>(() =>
            IauEarthOrientation.Load(Path.Combine(Path.GetTempPath(), "sn6-no-such-file.json")));
    }

    [Fact]
    public void TamperedAsset_Rejected()
    {
        string tmp = Path.GetTempFileName();
        try
        {
            File.Copy(Sn6Paths.OrientationAssetPath(), tmp, overwrite: true);
            var bytes = File.ReadAllBytes(tmp);
            bytes[bytes.Length - 20] ^= 0xFF; // corrupt one byte
            File.WriteAllBytes(tmp, bytes);
            Assert.Throws<InvalidDataException>(() => IauEarthOrientation.Load(tmp));
        }
        finally
        {
            File.Delete(tmp);
        }
    }

    [Fact]
    public void Angles_EvaluateToExpectedValues_AtJ2000()
    {
        var model = IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        model.EvaluateAngles(0.0,
            out double ra, out double dec, out double w,
            out double raRate, out double decRate, out double wRate);
        Assert.Equal(0.0, ra, 15);
        Assert.Equal(Math.PI / 2.0, dec, 12);
        Assert.Equal(190.147 * Math.PI / 180.0, w, 12);
        // Earth PM rate verbatim from the PCK: 360.9856235 deg/day in rad/s.
        double expectedWRate = 360.9856235 * Math.PI / 180.0 / 86400.0;
        Assert.True(Math.Abs(wRate - expectedWRate) < 1e-18, $"wRate={wRate:R}");
        Assert.True(raRate < 0 && decRate < 0); // slow polar precession drift
    }
}
