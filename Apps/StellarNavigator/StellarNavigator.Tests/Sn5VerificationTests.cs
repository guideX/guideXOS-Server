using System.Text.Json;
using Astronomy.Time;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// SN5 independent CSPICE verification: every expected value comes from
/// CSPICE N0067 (str2et/et2utc/deltet) loaded with the same authenticated
/// naif0012.tls — never from the managed converter under test.
/// Initial target: 100 us absolute UTC-to-ET difference.
/// </summary>
public class Sn5VerificationTests
{
    private const double ToleranceSeconds = 100e-6;
    private readonly ITestOutputHelper _out;

    public Sn5VerificationTests(ITestOutputHelper output) { _out = output; }

    private sealed record ForwardCase(string Utc, string Tag, double Et, double Deltet, string Regime);
    private sealed record InverseCase(double Et, string UtcIsoc3);
    private sealed record InvalidCase(string Utc);

    private static (List<ForwardCase> Fwd, List<InverseCase> Inv, List<InvalidCase> Bad,
        int LeapBoundary, int ForwardTotal) LoadFixture()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Sn5Paths.Sn5ReferencePath()));
        var root = doc.RootElement;
        var fwd = root.GetProperty("forward").EnumerateArray()
            .Select(e => new ForwardCase(
                e.GetProperty("utc").GetString()!,
                e.GetProperty("tag").GetString()!,
                e.GetProperty("et").GetDouble(),
                e.GetProperty("deltet").GetDouble(),
                e.GetProperty("regime").GetString()!)).ToList();
        var inv = root.GetProperty("inverse").EnumerateArray()
            .Select(e => new InverseCase(
                e.GetProperty("et").GetDouble(),
                e.GetProperty("utcIsoc3").GetString()!)).ToList();
        var bad = root.GetProperty("invalid").EnumerateArray()
            .Select(e => new InvalidCase(e.GetProperty("utc").GetString()!)).ToList();
        int leap = fwd.Count(c => c.Tag is "leap" or "leap2016");
        return (fwd, inv, bad, leap, fwd.Count);
    }

    [Fact]
    public void Sn5Fixture_MeetsComparisonCounts()
    {
        var (_, _, _, leap, total) = LoadFixture();
        Assert.True(total >= 150, $"forward comparisons: {total}");
        Assert.True(leap >= 30, $"leap-boundary cases: {leap}");
    }

    [Fact]
    public void UtcToEt_AgreesWithCspiceWithinTolerance()
    {
        var c = Sn5Paths.LoadConverter();
        var (fwd, _, _, _, _) = LoadFixture();
        double maxErr = 0, sumSq = 0;
        string worst = "";
        int provisional = 0, strict = 0;
        foreach (var fc in fwd)
        {
            bool allowProv = fc.Regime == "provisional";
            bool ok = c.TryUtcToEt(fc.Utc + "Z", out var r, out var err, allowProv);
            Assert.True(ok, $"{fc.Utc}: {err}");
            Assert.NotNull(r);
            double e = Math.Abs(r!.Et.Value - fc.Et);
            if (e > maxErr)
            {
                maxErr = e;
                worst = fc.Utc;
            }
            sumSq += e * e;
            Assert.True(e <= ToleranceSeconds, $"{fc.Utc}: err {e:E3} s exceeds 100 us");
            double de = Math.Abs(r.DeltaEtSeconds - fc.Deltet);
            Assert.True(de <= ToleranceSeconds, $"{fc.Utc}: deltet err {de:E3} s");
            Assert.Equal(fc.Regime == "provisional"
                ? TimeConversionQuality.ProvisionalStaleTable
                : TimeConversionQuality.Validated, r.Quality);
            if (allowProv)
                provisional++;
            else
                strict++;
        }
        double rms = Math.Sqrt(sumSq / fwd.Count);
        _out.WriteLine($"SN5 UTC->ET: n={fwd.Count} (strict={strict}, provisional={provisional}) " +
            $"max={maxErr:E3} s at {worst}, rms={rms:E3} s, tolerance=100 us");
        Assert.True(maxErr <= ToleranceSeconds);
    }

    [Fact]
    public void UtcToEt_LeapBoundaryCasesAgreeWithCspice()
    {
        var c = Sn5Paths.LoadConverter();
        var (fwd, _, _, _, _) = LoadFixture();
        var leap = fwd.Where(x => x.Tag is "leap" or "leap2016").ToList();
        Assert.True(leap.Count >= 30);
        double maxErr = 0;
        string worst = "";
        foreach (var fc in leap)
        {
            bool ok = c.TryUtcToEt(fc.Utc + "Z", out var r, out var err,
                allowProvisionalFuture: fc.Regime == "provisional");
            Assert.True(ok, $"{fc.Utc}: {err}");
            double e = Math.Abs(r!.Et.Value - fc.Et);
            if (e > maxErr)
            {
                maxErr = e;
                worst = fc.Utc;
            }
            Assert.True(e <= ToleranceSeconds, $"{fc.Utc}: err {e:E3} s");
        }
        _out.WriteLine($"SN5 leap-boundary: n={leap.Count} max={maxErr:E3} s at {worst}");
    }

    [Fact]
    public void EtToUtc_AgreesWithCspiceIsocStrings()
    {
        var c = Sn5Paths.LoadConverter();
        var (_, inv, _, _, _) = LoadFixture();
        int checkedCount = 0;
        foreach (var ic in inv)
        {
            // All inverse ETs in the fixture fall in the strict interval
            // except contemporary ones; allow provisional to exercise both.
            bool ok = c.TryEtToUtc(ic.Et, out var r, out var err, allowProvisionalFuture: true);
            Assert.True(ok, $"ET {ic.Et}: {err}");
            Assert.NotNull(r);
            string got = r!.Utc.ToFixedString(3);
            Assert.Equal(ic.UtcIsoc3 + "Z", got);
            checkedCount++;
        }
        _out.WriteLine($"SN5 ET->UTC: n={checkedCount} exact ISOC(3) string agreements");
    }

    [Fact]
    public void UtcEt_RoundTripThroughManagedConverter()
    {
        var c = Sn5Paths.LoadConverter();
        var (fwd, _, _, _, _) = LoadFixture();
        double maxRt = 0;
        foreach (var fc in fwd)
        {
            Assert.True(c.TryUtcToEt(fc.Utc + "Z", out var r, out _,
                allowProvisionalFuture: fc.Regime == "provisional"));
            Assert.True(c.TryEtToUtc(r!.Et.Value, out var inv, out _,
                allowProvisionalFuture: fc.Regime == "provisional"));
            Assert.True(c.TryUtcToEt(inv!.Utc.ToCanonicalString(), out var rt, out _,
                allowProvisionalFuture: fc.Regime == "provisional"));
            double e = Math.Abs(rt!.Et.Value - r.Et.Value);
            maxRt = Math.Max(maxRt, e);
            Assert.True(e <= 1e-6, $"{fc.Utc}: round-trip err {e:E3} s");
        }
        _out.WriteLine($"SN5 round-trip: n={fwd.Count} max={maxRt:E3} s");
    }

    [Fact]
    public void InvalidLeapInputs_AreRejected()
    {
        var c = Sn5Paths.LoadConverter();
        var (_, _, bad, _, _) = LoadFixture();
        Assert.True(bad.Count >= 10);
        foreach (var bc in bad)
        {
            // Invalid inputs must fail in strict AND provisional modes:
            // no silent downgrade, no normalization.
            Assert.False(c.TryUtcToEt(bc.Utc, out _, out var e1),
                $"'{bc.Utc}' unexpectedly accepted (strict)");
            Assert.False(c.TryUtcToEt(bc.Utc, out _, out var e2, allowProvisionalFuture: true),
                $"'{bc.Utc}' unexpectedly accepted (provisional)");
            Assert.NotNull(e1);
        }
        _out.WriteLine($"SN5 invalid inputs rejected: n={bad.Count}");
    }
}
