using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// Provider behavior: dataset trust, coverage/boundary handling, center
/// chains, failure modes, and determinism. Numerical agreement with JPL is
/// covered independently by ChebyshevVerificationTests.
/// </summary>
public class ChebyshevProviderTests : IDisposable
{
    private readonly string _datasetPath;

    public ChebyshevProviderTests()
    {
        _datasetPath = ResolveDatasetPath();
    }

    public void Dispose() { }

    internal static string ResolveDatasetPath()
    {
        var src = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
            "Astronomy.Ephemeris", "Data", "ephemeris_de440s_chebyshev.json");
        if (File.Exists(src))
            return Path.GetFullPath(src);
        var local = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s_chebyshev.json");
        if (File.Exists(local))
            return local;
        throw new FileNotFoundException("SN3 Chebyshev dataset not found.");
    }

    [Fact]
    public void Dataset_LoadsAndReportsProvenance()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        Assert.Contains("DE440s", provider.DataSource);
        Assert.Contains("Type-2", provider.DataSource);
        Assert.Equal(ChebyshevEphemerisProvider.ExpectedDatasetSha256, provider.Sha256);
        Assert.Equal(75350, provider.TotalRecords);
        Assert.Equal(ReferenceFrameKind.Barycentric, provider.NativeFrame.Kind);
    }

    [Fact]
    public void Dataset_SupportedBodies_MatchSn2SetPlusChainHelper()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        foreach (var body in new[] { "sun", "earth", "moon", "mars", "jupiter", "ssb" })
            Assert.True(provider.IsBodySupported(body), body);
        Assert.True(provider.IsBodySupported("emb"));
        Assert.False(provider.IsBodySupported("pluto"));
        Assert.False(provider.IsBodySupported("saturn"));
    }

    [Fact]
    public void Dataset_Coverage_SpansFullKernelInterval()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var cov = provider.GetCoverage("earth");
        Assert.NotNull(cov);
        double spanDays = cov!.Value.End.JdUtc - cov.Value.Start.JdUtc;
        Assert.True(spanDays > 109500 && spanDays < 109700, $"span {spanDays}");
        Assert.Null(provider.GetCoverage("pluto"));
    }

    [Fact]
    public void GetState_J2000_EarthDistanceIsOneAu()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(NavigationResultStatus.ValidatedReference, result.Status);
        double au = result.Value!.Position.Magnitude / Distance.MetersPerAstronomicalUnit;
        Assert.True(au is > 0.98 and < 1.02, $"Earth distance {au} AU");
    }

    [Fact]
    public void GetState_AcceptsOffGridEpoch()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0 + 12.3456789, TimeScale.Tdb);
        var result = provider.GetState("mars", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.True(double.IsFinite(result.Value!.Position.Magnitude));
        Assert.True(double.IsFinite(result.Value!.Velocity.Magnitude));
        Assert.True(result.Value!.Velocity.Magnitude > 0);
    }

    [Fact]
    public void GetState_AcceptsIcrfJ2000Frame()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        var bary = provider.GetState("sun", epoch, ReferenceFrame.Barycentric);
        var icrf = provider.GetState("sun", epoch, ReferenceFrame.IcrfJ2000);
        Assert.True(bary.IsSuccess && icrf.IsSuccess);
        Assert.Equal(bary.Value!.Position.X, icrf.Value!.Position.X, 6);
    }

    [Fact]
    public void GetState_FirstAndLastCoverageEpochs_Succeed()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        foreach (var body in new[] { "sun", "earth", "moon", "mars", "jupiter" })
        {
            var first = provider.GetState(body, new AstroTime(2396752.5, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(first.IsSuccess, $"{body} first: {first.ErrorMessage}");
            var last = provider.GetState(body, new AstroTime(2506352.5, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(last.IsSuccess, $"{body} last: {last.ErrorMessage}");
        }
    }

    [Fact]
    public void GetState_ExactCoverageEnd_SelectsFinalRecord()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        // END = INIT + N*INTLEN for every segment: -4734072000 + 9469440000 s.
        double endEt = -4734072000.0 + 9469440000.0;
        var epoch = new AstroTime(2451545.0 + endEt / 86400.0, TimeScale.Tdb);
        var result = provider.GetState("moon", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess, result.ErrorMessage);
    }

    [Fact]
    public void GetState_OutsideCoverage_FailsWithoutExtrapolation()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var before = provider.GetState("earth",
            new AstroTime(2396752.5 - 1.0, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.False(before.IsSuccess);
        Assert.Equal(NavigationResultStatus.StaleData, before.Status);

        var after = provider.GetState("earth",
            new AstroTime(2506352.5 + 1.0, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.False(after.IsSuccess);
        Assert.Equal(NavigationResultStatus.StaleData, after.Status);
    }

    [Fact]
    public void GetState_UnknownBody_UnsupportedFrame_NonFiniteEpoch_Fail()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);

        var unknown = provider.GetState("pluto", epoch, ReferenceFrame.Barycentric);
        Assert.False(unknown.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, unknown.Status);

        var frame = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);
        Assert.False(frame.IsSuccess);
        Assert.Equal(NavigationResultStatus.UnsupportedCalculation, frame.Status);

        var nan = provider.GetState("earth", new AstroTime(double.NaN, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.False(nan.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, nan.Status);
    }

    [Fact]
    public void GetState_Ssb_IsZeroWithinCoverage()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0 + 123.456, TimeScale.Tdb);
        var result = provider.GetState("ssb", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);
        Assert.Equal(0.0, result.Value!.Position.Magnitude, 12);
        Assert.Equal(0.0, result.Value!.Velocity.Magnitude, 12);
    }

    [Fact]
    public void GetRelativeState_MoonEarth_MatchesComponentDifference()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0 + 77.777, TimeScale.Tdb);
        var rel = provider.GetRelativeState("moon", "earth", epoch);
        Assert.True(rel.IsSuccess, rel.ErrorMessage);
        var moon = provider.GetState("moon", epoch, ReferenceFrame.Barycentric);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var (relPos, relVel) = rel.Value!;
        var expected = (moon.Value!.Position - earth.Value!.Position).Magnitude;
        Assert.Equal(expected, relPos.Magnitude, 6);
        Assert.True(relPos.Magnitude / Distance.MetersPerKilometer is > 350000 and < 410000);
        Assert.True(double.IsFinite(relVel.Magnitude));
    }

    [Fact]
    public void GetState_IsDeterministic()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var epoch = new AstroTime(2451545.0 + 3.14159265, TimeScale.Tdb);
        var r1 = provider.GetState("jupiter", epoch, ReferenceFrame.Barycentric);
        var r2 = provider.GetState("jupiter", epoch, ReferenceFrame.Barycentric);
        Assert.True(r1.IsSuccess && r2.IsSuccess);
        Assert.Equal(r1.Value!.Position.X, r2.Value!.Position.X, 15);
        Assert.Equal(r1.Value!.Velocity.Z, r2.Value!.Velocity.Z, 15);
    }

    [Fact]
    public void Constructor_RejectsChecksumMismatch()
    {
        var tmp = Path.Combine(Path.GetTempPath(), "sn3_bad_" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            File.WriteAllText(tmp, "{\"metadata\":{},\"segments\":[]}");
            Assert.Throws<InvalidDataException>(() => new ChebyshevEphemerisProvider(tmp));
        }
        finally
        {
            if (File.Exists(tmp))
                File.Delete(tmp);
        }
    }

    [Fact]
    public void Constructor_RejectsCorruptJson()
    {
        var tmp = Path.Combine(Path.GetTempPath(), "sn3_corrupt_" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            File.WriteAllText(tmp, "{not valid json");
            Assert.Throws<InvalidDataException>(
                () => new ChebyshevEphemerisProvider(tmp, expectedSha256: null));
        }
        finally
        {
            if (File.Exists(tmp))
                File.Delete(tmp);
        }
    }

    [Fact]
    public void Constructor_RejectsDegreeLengthMismatch()
    {
        var tmp = Path.Combine(Path.GetTempPath(), "sn3_deg_" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            File.WriteAllText(tmp,
                "{\"metadata\":{\"source\":\"t\",\"totalRecords\":1}," +
                "\"segments\":[{\"bodyId\":\"x\",\"targetId\":99,\"centerId\":0,\"centerBodyId\":\"ssb\"," +
                "\"frameId\":1,\"frame\":\"J2000\",\"segmentType\":2,\"degree\":12," +
                "\"recordIntervalSeconds\":100.0,\"initEt\":0.0,\"recordCount\":1," +
                "\"coverageStartJd\":2451545.0,\"coverageEndJd\":2451546.0," +
                "\"records\":[[0.0,50.0,1.0,2.0]]}]}");
            Assert.Throws<InvalidDataException>(
                () => new ChebyshevEphemerisProvider(tmp, expectedSha256: null));
        }
        finally
        {
            if (File.Exists(tmp))
                File.Delete(tmp);
        }
    }

    [Fact]
    public void Constructor_RejectsMissingFile()
    {
        Assert.Throws<FileNotFoundException>(() =>
            new ChebyshevEphemerisProvider(Path.Combine(Path.GetTempPath(), "sn3_nope_missing.json")));
    }
}
