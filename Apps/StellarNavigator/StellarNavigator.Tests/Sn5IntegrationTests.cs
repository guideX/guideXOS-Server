using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// SN5 application integration, offline/fault behavior, and performance:
/// end-to-end UTC-to-planet-state queries through the untouched SN4 engine,
/// quality separation, missing/corrupt time data, and resource usage.
/// </summary>
public class Sn5IntegrationTests
{
    private readonly ITestOutputHelper _out;

    public Sn5IntegrationTests(ITestOutputHelper output) { _out = output; }

    private static IndexedChebyshevEphemerisProvider LoadEphemeris() =>
        new(Sn4Paths.DatasetPath());

    [Fact]
    public void UtcQuery_EarthStrictUtcAgreesWithDirectJdQuery()
    {
        using var eph = LoadEphemeris();
        var query = new UtcEphemerisQuery(eph, Sn5Paths.LoadConverter());
        var resolved = query.GetState("earth", "2016-07-14T00:00:00Z", ReferenceFrame.Barycentric);
        Assert.True(resolved.IsSuccess);
        Assert.Equal(TimeConversionQuality.Validated, resolved.TimeQuality);
        Assert.NotNull(resolved.Time);
        Assert.NotNull(resolved.Ephemeris);
        Assert.True(resolved.Ephemeris!.IsSuccess);
        // Validated time + validated ephemeris keeps ValidatedReference (same
        // JD-double semantics as SN4 direct queries).
        Assert.Equal(NavigationResultStatus.ValidatedReference, resolved.Ephemeris.Status);

        var direct = eph.GetState("earth",
            new AstroTime(resolved.Time!.JdTdb, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.True(direct.IsSuccess);
        Assert.Equal(direct.Value.Position.X, resolved.Ephemeris.Value.Position.X);
        Assert.Equal(direct.Value.Position.Y, resolved.Ephemeris.Value.Position.Y);
        Assert.Equal(direct.Value.Position.Z, resolved.Ephemeris.Value.Position.Z);
        Assert.Equal(direct.Value.Velocity.X, resolved.Ephemeris.Value.Velocity.X);
    }

    [Fact]
    public void UtcQuery_JupiterAtLeapSecond()
    {
        using var eph = LoadEphemeris();
        var query = new UtcEphemerisQuery(eph, Sn5Paths.LoadConverter());
        var resolved = query.GetState("jupiter", "2016-12-31T23:59:60Z", ReferenceFrame.Barycentric);
        Assert.True(resolved.IsSuccess);
        Assert.Equal(TimeConversionQuality.Validated, resolved.TimeQuality);
        Assert.True(resolved.Ephemeris!.IsSuccess);
        Assert.True(resolved.Ephemeris.Value.Position.Magnitude > 7e11); // ~5 AU in meters
    }

    [Fact]
    public void UtcQuery_ProvisionalEpochDowngradesEphemerisQuality()
    {
        using var eph = LoadEphemeris();
        var query = new UtcEphemerisQuery(eph, Sn5Paths.LoadConverter());
        var resolved = query.GetState("earth", "2024-01-01T00:00:00Z",
            ReferenceFrame.Barycentric, allowProvisionalFuture: true);
        Assert.True(resolved.IsSuccess);
        Assert.Equal(TimeConversionQuality.ProvisionalStaleTable, resolved.TimeQuality);
        // The polynomial evaluation is sound but the epoch is assumed:
        // never reported as ValidatedReference.
        Assert.Equal(NavigationResultStatus.StaleData, resolved.Ephemeris!.Status);
        Assert.Contains("provisional", resolved.Ephemeris.Provenance ?? "", StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void UtcQuery_UnsupportedEpochsFailWithTypedDiagnostics()
    {
        using var eph = LoadEphemeris();
        var query = new UtcEphemerisQuery(eph, Sn5Paths.LoadConverter());

        var hist = query.GetState("earth", "1969-07-20T20:17:40Z", ReferenceFrame.Barycentric);
        Assert.False(hist.IsSuccess);
        Assert.Equal(TimeConversionQuality.UnsupportedHistorical, hist.TimeQuality);
        Assert.NotNull(hist.TimeError);

        var future = query.GetState("earth", "2030-01-01T00:00:00Z", ReferenceFrame.Barycentric);
        Assert.False(future.IsSuccess);
        Assert.Equal(TimeConversionQuality.UnsupportedFuture, future.TimeQuality);

        var bad = query.GetState("earth", "2019-12-31T23:59:60Z", ReferenceFrame.Barycentric);
        Assert.False(bad.IsSuccess);
        Assert.Equal(TimeConversionQuality.InvalidUtcInput, bad.TimeQuality);
    }

    [Fact]
    public void UtcQuery_TimeDataFailureDoesNotBreakDirectTdbQueries()
    {
        using var eph = LoadEphemeris();
        // A missing time-data asset fails only the UTC path...
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondProvider.Load(Path.Combine(Path.GetTempPath(), "sn5-missing.sn5json")));
        // ...while direct JD TDB queries on the SN4 engine keep working.
        var direct = eph.GetState("mars",
            new AstroTime(2451545.0 + 12.3456789, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.True(direct.IsSuccess);
        Assert.Equal(NavigationResultStatus.ValidatedReference, direct.Status);
    }

    [Fact]
    public void TimeSubsystem_PerformanceAndFootprint()
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var provider = Sn5Paths.LoadProvider();
        sw.Stop();
        long loadUs = sw.ElapsedTicks * 1_000_000 / System.Diagnostics.Stopwatch.Frequency;

        var converter = new TimeConverter(provider.Table, provider.Identity);
        const int n = 20000;
        sw.Restart();
        for (int i = 0; i < n; i++)
            Assert.True(converter.TryUtcToEt("2016-07-14T00:00:00Z", out _, out _));
        sw.Stop();
        double fwdUs = sw.Elapsed.TotalMicroseconds / n;
        sw.Restart();
        for (int i = 0; i < n; i++)
            Assert.True(converter.TryEtToUtc(536500868.18, out _, out _));
        sw.Stop();
        double invUs = sw.Elapsed.TotalMicroseconds / n;

        _out.WriteLine($"SN5 time-data: {provider.AssetBytes} bytes, load {loadUs} us, " +
            $"UTC->ET {fwdUs:F2} us, ET->UTC {invUs:F2} us (n={n})");
        Assert.True(provider.AssetBytes < 8192);
        Assert.True(fwdUs < 100.0 && invUs < 100.0); // generous functionality bound
    }
}
