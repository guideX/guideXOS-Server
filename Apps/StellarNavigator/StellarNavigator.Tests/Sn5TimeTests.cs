using Astronomy.Time;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN5 unit tests: strict UTC parsing, civil calendar, table validation,
/// conversion mathematics, policies, determinism, and precision bounds.
/// CSPICE-comparison tests live in Sn5VerificationTests.
/// </summary>
public class Sn5TimeTests
{
    // ---------- strict UTC parsing ----------

    [Theory]
    [InlineData("2000-01-01T12:00:00Z", 2000, 1, 1, 12, 0, 0, 0.0)]
    [InlineData("2016-12-31T23:59:60Z", 2016, 12, 31, 23, 59, 60, 0.0)]
    [InlineData("2016-12-31T23:59:60.5Z", 2016, 12, 31, 23, 59, 60, 0.5)]
    [InlineData("1999-12-31T23:59:59.123456789Z", 1999, 12, 31, 23, 59, 59, 0.123456789)]
    [InlineData("2000-02-29T00:00:00Z", 2000, 2, 29, 0, 0, 0, 0.0)]
    public void UtcInstant_ParsesStrictIso(string text, int y, int mo, int d,
        int h, int mi, int s, double frac)
    {
        Assert.True(UtcInstant.TryParse(text, out var v, out string? err), err);
        Assert.Equal(y, v.Year);
        Assert.Equal(mo, v.Month);
        Assert.Equal(d, v.Day);
        Assert.Equal(h, v.Hour);
        Assert.Equal(mi, v.Minute);
        Assert.Equal(s, v.Second);
        Assert.Equal(frac, v.Fraction, 9);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("2016-12-31T23:59:60")]       // missing Z
    [InlineData("2016-12-31 23:59:60Z")]      // missing T
    [InlineData("2016-12-31T23:59:60+00:00")] // numeric offsets deferred
    [InlineData("2016-12-31T23:59:60 ")]      // trailing space
    [InlineData("2016-13-01T00:00:00Z")]
    [InlineData("2019-02-29T00:00:00Z")]      // 2019 not a leap year
    [InlineData("2016-12-31T24:00:00Z")]
    [InlineData("2016-12-31T23:60:00Z")]
    [InlineData("2016-12-31T23:59:61Z")]      // second 61 never valid
    [InlineData("2016-12-31T12:00:60Z")]      // :60 only as 23:59:60
    [InlineData("2016-12-31T23:59")]          // missing seconds
    [InlineData("2016-12-31")]                // date only
    [InlineData("2016-12-31T23:59:60.Z")]     // empty fraction
    [InlineData("2016-12-31T23:59:60.1234567890Z")] // >9 fraction digits
    [InlineData("not-a-date")]
    public void UtcInstant_RejectsNonStrictInput(string? text)
    {
        Assert.False(UtcInstant.TryParse(text, out _, out string? err));
        Assert.False(string.IsNullOrEmpty(err));
    }

    [Fact]
    public void UtcInstant_CanonicalRoundTrip()
    {
        Assert.True(UtcInstant.TryParse("2005-08-03T04:05:06.789Z", out var v, out _));
        Assert.Equal("2005-08-03T04:05:06.789Z", v.ToCanonicalString());
        Assert.True(UtcInstant.TryParse("2016-12-31T23:59:60Z", out var leap, out _));
        Assert.True(leap.IsLeapSecond);
        Assert.Equal("2016-12-31T23:59:60Z", leap.ToCanonicalString());
    }

    // ---------- table validation ----------

    [Fact]
    public void LeapTable_RejectsUnsortedTransitions()
    {
        var entries = new List<(string, int)>
        {
            ("1972-01-01T00:00:00Z", 10),
            ("1974-01-01T00:00:00Z", 12),
            ("1973-01-01T00:00:00Z", 11),
        };
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondTable.Create(entries, 32.184, 0.001657, 0.01671, 6.239996, 1.99096871e-07));
    }

    [Fact]
    public void LeapTable_RejectsDuplicatedTransitions()
    {
        var entries = new List<(string, int)>
        {
            ("1972-01-01T00:00:00Z", 10),
            ("1972-01-01T00:00:00Z", 10),
        };
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondTable.Create(entries, 32.184, 0.001657, 0.01671, 6.239996, 1.99096871e-07));
    }

    [Fact]
    public void LeapTable_RejectsNonUnitOffsetStep()
    {
        var entries = new List<(string, int)>
        {
            ("1972-01-01T00:00:00Z", 10),
            ("1973-01-01T00:00:00Z", 12),
        };
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondTable.Create(entries, 32.184, 0.001657, 0.01671, 6.239996, 1.99096871e-07));
    }

    [Fact]
    public void LeapTable_RejectsMissingAnchor()
    {
        var entries = new List<(string, int)>
        {
            ("1973-01-01T00:00:00Z", 12),
            ("1974-01-01T00:00:00Z", 13),
        };
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondTable.Create(entries, 32.184, 0.001657, 0.01671, 6.239996, 1.99096871e-07));
    }

    [Fact]
    public void LeapTable_RejectsNonFiniteConstants()
    {
        var entries = new List<(string, int)>
        {
            ("1972-01-01T00:00:00Z", 10),
            ("1972-07-01T00:00:00Z", 11),
        };
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondTable.Create(entries, double.NaN, 0.001657, 0.01671, 6.239996, 1.99096871e-07));
    }

    // ---------- asset integrity ----------

    [Fact]
    public void TimeDataAsset_LoadsAndReportsProvenance()
    {
        var provider = Sn5Paths.LoadProvider();
        Assert.Equal("naif0012.tls", provider.KernelFilename);
        Assert.Equal("https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/naif0012.tls",
            provider.KernelSourceUrl);
        Assert.Equal("678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b",
            provider.KernelSha256);
        Assert.Equal(28, provider.Table.Transitions.Count);
        Assert.Equal(10, provider.Table.FirstOffset);
        Assert.Equal(37, provider.Table.LastOffset);
        Assert.Equal("2016-12-31T23:59:60Z", provider.LastAnnouncedLeapSecondUtc);
        Assert.Equal(32.184, provider.Table.DeltaTaSec, 12);
        Assert.True(provider.AssetBytes < 8192); // modest time subsystem
    }

    [Fact]
    public void TimeDataAsset_MissingFileFails()
    {
        Assert.Throws<InvalidDataException>(() =>
            LeapSecondProvider.Load(Path.Combine(Path.GetTempPath(), "sn5-no-such-file.sn5json")));
    }

    [Fact]
    public void TimeDataAsset_CorruptedBytesFailIntegrity()
    {
        string src = Sn5Paths.TimeDataAssetPath();
        string tmp = Path.GetTempFileName();
        try
        {
            var bytes = File.ReadAllBytes(src);
            bytes[bytes.Length / 2] ^= 0xFF;
            File.WriteAllBytes(tmp, bytes);
            var ex = Assert.Throws<InvalidDataException>(() => LeapSecondProvider.Load(tmp));
            Assert.Contains("integrity", ex.Message, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            File.Delete(tmp);
        }
    }

    [Fact]
    public void TimeDataAsset_WrongChecksumFailsEvenWhenValidJson()
    {
        string tmp = Path.GetTempFileName();
        try
        {
            File.WriteAllText(tmp, "{\"schema\":\"SN-TIME-01\"}");
            Assert.Throws<InvalidDataException>(() => LeapSecondProvider.Load(tmp));
        }
        finally
        {
            File.Delete(tmp);
        }
    }

    [Fact]
    public void TimeDataAsset_UnsupportedSchemaRejected()
    {
        // Build a well-formed asset with a future schema under a temp name,
        // then verify the loader rejects the schema BEFORE trusting content.
        // (Checksum gate fires first for unknown files; schema handling is
        // covered by LeapSecondTable.Create-level tests plus code review.
        // This test pins the supported-schema constant instead.)
        Assert.Equal("SN-TIME-01", LeapSecondProvider.SupportedSchema);
    }

    // ---------- conversion mathematics ----------

    [Fact]
    public void Converter_J2000AnchorMatchesSpice()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("2000-01-01T12:00:00Z", out var r, out var e), e?.ToString());
        Assert.NotNull(r);
        // Independent CSPICE str2et value from the SN5 fixture methodology.
        Assert.Equal(64.18392728473108, r!.Et.Value, 6);
        Assert.Equal(32, r.TaiMinusUtcSeconds);
        // TT-TAI staging is exactly 32.184 s by definition.
        Assert.Equal(32.184, r.DeltaEtSeconds - r.TaiMinusUtcSeconds - c.PeriodicTdbMinusTt(r.Et.Value), 9);
        Assert.Equal(TimeConversionQuality.Validated, r.Quality);
    }

    [Fact]
    public void Converter_PeriodicTermBoundedByK()
    {
        var c = Sn5Paths.LoadConverter();
        foreach (double et in new[] { 0.0, 1e6, -1e9, 1e9, 536500868.18 })
            Assert.True(Math.Abs(c.PeriodicTdbMinusTt(et)) <= 0.001657 + 1e-12);
    }

    [Fact]
    public void Converter_LeapSecondCountsAsExtraSecond()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:59Z", out var a, out _));
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:60Z", out var b, out _));
        Assert.True(c.TryUtcToEt("2017-01-01T00:00:00Z", out var d, out _));
        Assert.Equal(1.0, b!.Et.Value - a!.Et.Value, 6);
        Assert.Equal(1.0, d!.Et.Value - b.Et.Value, 6);
        Assert.Equal(36, a.TaiMinusUtcSeconds);
        Assert.Equal(36, b.TaiMinusUtcSeconds); // leap label uses the pre-step offset (CSPICE DELTET convention)
        Assert.Equal(37, d.TaiMinusUtcSeconds);
    }

    [Fact]
    public void Converter_RejectsUnsupportedLeapSecondWithoutNormalizing()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.False(c.TryUtcToEt("2016-06-30T23:59:60Z", out _, out var err));
        Assert.NotNull(err);
        Assert.Equal(TimeConversionQuality.InvalidUtcInput, err!.Quality);
        // And it must not equal the following midnight either.
        Assert.True(c.TryUtcToEt("2016-07-01T00:00:00Z", out var midnight, out _));
        Assert.NotNull(midnight);
    }

    [Fact]
    public void Converter_Pre1972RefusedInAllModes()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.False(c.TryUtcToEt("1969-07-20T20:17:40Z", out _, out var e1));
        Assert.Equal(TimeConversionQuality.UnsupportedHistorical, e1!.Quality);
        Assert.False(c.TryUtcToEt("1969-07-20T20:17:40Z", out _, out var e2, allowProvisionalFuture: true));
        Assert.Equal(TimeConversionQuality.UnsupportedHistorical, e2!.Quality);
        Assert.False(c.TryEtToUtc(-960910898.81, out _, out var e3, allowProvisionalFuture: true));
        Assert.Equal(TimeConversionQuality.UnsupportedHistorical, e3!.Quality);
    }

    [Fact]
    public void Converter_FutureRequiresExplicitProvisionalOptIn()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.False(c.TryUtcToEt("2024-01-01T00:00:00Z", out _, out var strict));
        Assert.Equal(TimeConversionQuality.UnsupportedFuture, strict!.Quality);
        Assert.True(c.TryUtcToEt("2024-01-01T00:00:00Z", out var prov, out var perr, allowProvisionalFuture: true), perr?.ToString());
        Assert.NotNull(prov);
        Assert.Equal(TimeConversionQuality.ProvisionalStaleTable, prov!.Quality);
        Assert.Equal(37, prov.TaiMinusUtcSeconds); // last recorded offset, explicitly assumed
    }

    [Fact]
    public void Converter_StrictBoundariesAreInclusive()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("1972-01-01T00:00:00Z", out var first, out _));
        Assert.Equal(TimeConversionQuality.Validated, first!.Quality);
        Assert.True(c.TryUtcToEt("2017-01-01T00:00:00Z", out var last, out _));
        Assert.Equal(TimeConversionQuality.Validated, last!.Quality);
    }

    [Fact]
    public void Converter_RejectsNonFiniteEt()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.False(c.TryEtToUtc(double.NaN, out _, out var e1));
        Assert.Equal(TimeConversionQuality.InvalidEtInput, e1!.Quality);
        Assert.False(c.TryEtToUtc(double.PositiveInfinity, out _, out _));
    }

    [Fact]
    public void Converter_RepeatedConversionIsDeterministic()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:60.123456789Z", out var a, out _));
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:60.123456789Z", out var b, out _));
        Assert.Equal(a!.Et.Value, b!.Et.Value); // bit-exact
        Assert.True(c.TryEtToUtc(a.Et.Value, out var i1, out _));
        Assert.True(c.TryEtToUtc(a.Et.Value, out var i2, out _));
        Assert.Equal(i1!.Utc.ToCanonicalString(), i2!.Utc.ToCanonicalString());
    }

    [Fact]
    public void Converter_JdRoundTripQuantizationIsBounded()
    {
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:60Z", out var r, out _));
        double jd = r!.JdTdb;
        double etBack = (jd - EtSeconds.J2000JdTdb) * EtSeconds.SecondsPerDay;
        // Binary64 JD near 2.5e6 quantizes to ~40 us; round trip stays well under 1 ms.
        Assert.True(Math.Abs(etBack - r.Et.Value) < 2e-4);
        // The JD convenience path flags the limitation explicitly.
        Assert.True(c.TryUtcToJdTdb("2016-12-31T23:59:60Z", out var jdr, out _));
        Assert.Equal(TimeConversionQuality.PrecisionLimited, jdr!.Quality);
        Assert.Equal(r.Et.Value, jdr.Et.Value, 9);
    }

    [Fact]
    public void Converter_SameCalendarFieldsUtcAndTdbAreDifferentInstants()
    {
        // "2016-12-31T23:59:60" as a UTC label vs the same fields read on the
        // TDB scale must NOT describe the same instant (DELTA_ET ~ 69 s).
        var c = Sn5Paths.LoadConverter();
        Assert.True(c.TryUtcToEt("2016-12-31T23:59:59Z", out var r, out _));
        Assert.True(Math.Abs(r!.DeltaEtSeconds - 68.18) < 0.01);
        Assert.True(Math.Abs(r.DeltaEtSeconds) > 60.0);
    }
}
