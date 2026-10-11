using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// Indexed (lazy-loading) provider behavior: eight-planet support, coverage,
/// failure modes, determinism, disposal, concurrency, and bounded-cache
/// guarantees. Numerical agreement with CSPICE is covered independently by
/// Sn4VerificationTests; agreement with the SN3 provider by
/// LazyEquivalenceTests.
/// </summary>
public class IndexedProviderTests : IDisposable
{
    private readonly string _datasetPath;
    private readonly List<string> _tempFiles = new();

    public IndexedProviderTests()
    {
        _datasetPath = Sn4Paths.DatasetPath();
    }

    public void Dispose()
    {
        foreach (var f in _tempFiles)
        {
            try { if (File.Exists(f)) File.Delete(f); } catch { }
        }
    }

    private IndexedChebyshevEphemerisProvider Open(
        long budget = IndexedChebyshevEphemerisProvider.DefaultCacheBudgetBytes)
    {
        // Checksum verification stays enabled (trusted manifest default).
        return new IndexedChebyshevEphemerisProvider(_datasetPath, cacheBudgetBytes: budget);
    }

    private IndexedChebyshevEphemerisProvider OpenDefault() => Open();

    [Fact]
    public void Dataset_LoadsIndexOnly_AndReportsProvenance()
    {
        using var provider = OpenDefault();
        Assert.Contains("DE440s", provider.DataSource);
        Assert.Contains("indexed", provider.DataSource);
        Assert.Equal(IndexedChebyshevEphemerisProvider.ExpectedDatasetSha256, provider.Sha256);
        Assert.Equal(106177L, provider.TotalRecords);
        Assert.Equal(ReferenceFrameKind.Barycentric, provider.NativeFrame.Kind);
        Assert.Equal("3917ee56769db332790c751e2168843d", provider.Provenance.SpkMd5);
        // Index load materializes no coefficient records.
        Assert.Equal(0, provider.CachedRecordCount);
        Assert.Equal(0, provider.CachedBytes);
    }

    [Fact]
    public void Dataset_SupportsAllEightPlanets_PlusCenters()
    {
        using var provider = OpenDefault();
        foreach (var body in new[]
        {
            "mercury", "venus", "earth", "mars",
            "jupiter", "saturn", "uranus", "neptune",
            "sun", "moon", "emb", "mercury_center", "venus_center", "ssb",
        })
            Assert.True(provider.IsBodySupported(body), body);

        // Explicitly unsupported: Pluto (out of scope), Mars center (absent
        // from DE440s), outer-planet centers (absent from DE440s).
        foreach (var body in new[] { "pluto", "mars_center", "jupiter_center", "saturn_center" })
            Assert.False(provider.IsBodySupported(body), body);
    }

    [Fact]
    public void Dataset_Coverage_SpansFullKernelInterval()
    {
        using var provider = OpenDefault();
        foreach (var body in new[] { "mercury", "venus", "saturn", "uranus", "neptune" })
        {
            var cov = provider.GetCoverage(body);
            Assert.NotNull(cov);
            double spanDays = cov!.Value.End.JdUtc - cov.Value.Start.JdUtc;
            Assert.True(spanDays > 109500 && spanDays < 109700, $"{body} span {spanDays}");
        }
        Assert.Null(provider.GetCoverage("pluto"));
    }

    [Fact]
    public void GetState_NewPlanets_AreFinite_AtJ2000()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        foreach (var body in new[] { "mercury", "venus", "saturn", "uranus", "neptune" })
        {
            var result = provider.GetState(body, epoch, ReferenceFrame.Barycentric);
            Assert.True(result.IsSuccess, $"{body}: {result.ErrorMessage}");
            Assert.Equal(NavigationResultStatus.ValidatedReference, result.Status);
            Assert.True(double.IsFinite(result.Value!.Position.Magnitude));
            Assert.True(double.IsFinite(result.Value!.Velocity.Magnitude));
            Assert.True(result.Value!.Velocity.Magnitude > 0);
        }
        // Saturn well outside Earth's orbit; Mercury well inside.
        var saturn = provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        var mercury = provider.GetState("mercury", epoch, ReferenceFrame.Barycentric);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        double saturnAu = saturn.Value!.Position.Magnitude / Distance.MetersPerAstronomicalUnit;
        double mercuryAu = mercury.Value!.Position.Magnitude / Distance.MetersPerAstronomicalUnit;
        double earthAu = earth.Value!.Position.Magnitude / Distance.MetersPerAstronomicalUnit;
        Assert.True(saturnAu > earthAu, $"saturn {saturnAu} AU");
        Assert.True(mercuryAu < earthAu, $"mercury {mercuryAu} AU");
    }

    [Fact]
    public void GetState_NewPlanets_AcceptOffGridEpochs()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 12.3456789, TimeScale.Tdb);
        foreach (var body in new[] { "mercury", "venus", "saturn", "uranus", "neptune" })
        {
            var result = provider.GetState(body, epoch, ReferenceFrame.Barycentric);
            Assert.True(result.IsSuccess, $"{body}: {result.ErrorMessage}");
        }
    }

    [Fact]
    public void GetState_FirstAndLastCoverageEpochs_Succeed_ForNewPlanets()
    {
        using var provider = OpenDefault();
        foreach (var body in new[] { "mercury", "venus", "saturn", "uranus", "neptune", "mercury_center", "venus_center" })
        {
            var first = provider.GetState(body, new AstroTime(2396752.5, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(first.IsSuccess, $"{body} first: {first.ErrorMessage}");
            var last = provider.GetState(body, new AstroTime(2506352.5, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(last.IsSuccess, $"{body} last: {last.ErrorMessage}");
        }
    }

    [Fact]
    public void GetState_OutsideCoverage_FailsWithoutExtrapolation()
    {
        using var provider = OpenDefault();
        foreach (var body in new[] { "mercury", "saturn", "neptune" })
        {
            var before = provider.GetState(body,
                new AstroTime(2396752.5 - 1.0, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.False(before.IsSuccess);
            Assert.Equal(NavigationResultStatus.StaleData, before.Status);

            var after = provider.GetState(body,
                new AstroTime(2506352.5 + 1.0, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.False(after.IsSuccess);
            Assert.Equal(NavigationResultStatus.StaleData, after.Status);
        }
    }

    [Fact]
    public void GetState_UnknownBody_UnsupportedFrame_NonFiniteEpoch_Fail()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);

        var unknown = provider.GetState("pluto", epoch, ReferenceFrame.Barycentric);
        Assert.False(unknown.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, unknown.Status);

        var frame = provider.GetState("saturn", epoch, ReferenceFrame.Heliocentric);
        Assert.False(frame.IsSuccess);
        Assert.Equal(NavigationResultStatus.UnsupportedCalculation, frame.Status);

        var nan = provider.GetState("saturn", new AstroTime(double.NaN, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.False(nan.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, nan.Status);
    }

    [Fact]
    public void GetRelativeState_NewPlanetVsEarth_MatchesComponentDifference()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 77.777, TimeScale.Tdb);
        foreach (var body in new[] { "mercury", "venus", "saturn", "uranus", "neptune" })
        {
            var rel = provider.GetRelativeState(body, "earth", epoch);
            Assert.True(rel.IsSuccess, $"{body}: {rel.ErrorMessage}");
            var target = provider.GetState(body, epoch, ReferenceFrame.Barycentric);
            var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
            var expected = (target.Value!.Position - earth.Value!.Position).Magnitude;
            Assert.Equal(expected, rel.Value!.Position.Magnitude, 6);
        }
    }

    [Fact]
    public void GetState_IsDeterministic_AcrossCacheStates()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 3.14159265, TimeScale.Tdb);
        var cold = provider.GetState("uranus", epoch, ReferenceFrame.Barycentric);
        Assert.True(cold.IsSuccess);
        Assert.True(provider.CacheMissCount > 0);
        // Pressure the cache with unrelated queries, then repeat.
        var rng = new Random(7);
        for (int i = 0; i < 500; i++)
        {
            double jd = 2396752.5 + rng.NextDouble() * 109600.0;
            provider.GetState("neptune", new AstroTime(jd, TimeScale.Tdb), ReferenceFrame.Barycentric);
        }
        var warm = provider.GetState("uranus", epoch, ReferenceFrame.Barycentric);
        Assert.True(warm.IsSuccess);
        Assert.Equal(cold.Value!.Position.X, warm.Value!.Position.X, 15);
        Assert.Equal(cold.Value!.Position.Y, warm.Value!.Position.Y, 15);
        Assert.Equal(cold.Value!.Position.Z, warm.Value!.Position.Z, 15);
        Assert.Equal(cold.Value!.Velocity.Z, warm.Value!.Velocity.Z, 15);
    }

    [Fact]
    public void Cache_RepeatedQuery_Hits_AndStaysBounded()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 100.25, TimeScale.Tdb);
        provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        long missesAfterFirst = provider.CacheMissCount;
        Assert.True(missesAfterFirst > 0);
        Assert.Equal(0, provider.CacheHitCount);
        provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        Assert.True(provider.CacheHitCount > 0);
        Assert.Equal(missesAfterFirst, provider.CacheMissCount);
        Assert.True(provider.CachedBytes <= provider.CacheBudgetBytes);
        Assert.True(provider.CachedBytes <= IndexedChebyshevEphemerisProvider.DefaultCacheBudgetBytes);
    }

    [Fact]
    public void Cache_RandomEpochQueries_DoNotGrowUnbounded()
    {
        using var provider = OpenDefault();
        var bodies = new[] { "mercury", "venus", "saturn", "uranus", "neptune", "earth", "moon", "mars", "jupiter", "sun" };
        var rng = new Random(20261012);
        for (int i = 0; i < 3000; i++)
        {
            string body = bodies[rng.Next(bodies.Length)];
            double jd = 2396752.5 + rng.NextDouble() * 109600.0;
            var result = provider.GetState(body, new AstroTime(jd, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(result.IsSuccess, $"{body}: {result.ErrorMessage}");
        }
        Assert.True(provider.CachedBytes <= provider.CacheBudgetBytes);
        Assert.True(provider.CachedRecordCount * 8L <= provider.CacheBudgetBytes + 64 * 1024);
    }

    [Fact]
    public void Cache_TinyBudget_EvictsYetStaysCorrect()
    {
        // Budget fits ~2 records; every new record forces eviction.
        using var provider = Open(1024);
        var epoch = new AstroTime(2451545.0 + 12.3456789, TimeScale.Tdb);
        var saturn = provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        Assert.True(saturn.IsSuccess, saturn.ErrorMessage);
        var rng = new Random(11);
        for (int i = 0; i < 200; i++)
        {
            double jd = 2396752.5 + rng.NextDouble() * 109600.0;
            provider.GetState("mercury", new AstroTime(jd, TimeScale.Tdb), ReferenceFrame.Barycentric);
            Assert.True(provider.CachedBytes <= provider.CacheBudgetBytes);
        }
        var repeat = provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        Assert.True(repeat.IsSuccess);
        Assert.Equal(saturn.Value!.Position.X, repeat.Value!.Position.X, 15);
    }

    [Fact]
    public void Cache_ZeroBudget_DisablesCachingButStaysCorrect()
    {
        using var provider = Open(0);
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        var result = provider.GetState("venus", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(0, provider.CachedRecordCount);
        Assert.Equal(0, provider.CachedBytes);
    }

    [Fact]
    public void Cache_ClearCache_DropsEntries_ResultsUnchanged()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 55.5, TimeScale.Tdb);
        var before = provider.GetState("neptune", epoch, ReferenceFrame.Barycentric);
        Assert.True(before.IsSuccess);
        Assert.True(provider.CachedRecordCount > 0);
        provider.ClearCache();
        Assert.Equal(0, provider.CachedRecordCount);
        Assert.Equal(0, provider.CachedBytes);
        var after = provider.GetState("neptune", epoch, ReferenceFrame.Barycentric);
        Assert.True(after.IsSuccess);
        Assert.Equal(before.Value!.Position.X, after.Value!.Position.X, 15);
    }

    [Fact]
    public void Provider_Restarts_AgreeExactly()
    {
        var epoch = new AstroTime(2451545.0 + 200.75, TimeScale.Tdb);
        NavigationResult<(Vector3D Position, Vector3D Velocity)> first;
        using (var p1 = OpenDefault())
        {
            first = p1.GetState("mercury", epoch, ReferenceFrame.Barycentric);
        }
        Assert.True(first.IsSuccess);
        using (var p2 = OpenDefault())
        {
            var second = p2.GetState("mercury", epoch, ReferenceFrame.Barycentric);
            Assert.True(second.IsSuccess);
            Assert.Equal(first.Value!.Position.X, second.Value!.Position.X);
            Assert.Equal(first.Value!.Position.Y, second.Value!.Position.Y);
            Assert.Equal(first.Value!.Position.Z, second.Value!.Position.Z);
            Assert.Equal(first.Value!.Velocity.X, second.Value!.Velocity.X);
        }
    }

    [Fact]
    public void Provider_ConcurrentQueries_Agree()
    {
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 300.125, TimeScale.Tdb);
        var serial = provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
        Assert.True(serial.IsSuccess);
        double[] xs = new double[64];
        System.Threading.Tasks.Parallel.For(0, 64, i =>
        {
            var r = provider.GetState("saturn", epoch, ReferenceFrame.Barycentric);
            Assert.True(r.IsSuccess);
            xs[i] = r.Value!.Position.X;
        });
        foreach (var x in xs)
            Assert.Equal(serial.Value!.Position.X, x);
        Assert.True(provider.CachedBytes <= provider.CacheBudgetBytes);
    }

    [Fact]
    public void Dispose_QueriesThrow_AndReopenWorks()
    {
        var provider = new IndexedChebyshevEphemerisProvider(_datasetPath);
        provider.Dispose();
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        Assert.Throws<ObjectDisposedException>(() =>
            provider.GetState("earth", epoch, ReferenceFrame.Barycentric));
        Assert.Throws<ObjectDisposedException>(() => provider.IsBodySupported("earth"));
        provider.Dispose(); // second dispose is safe
        using var reopened = OpenDefault();
        var result = reopened.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);
    }

    [Fact]
    public void Constructor_RejectsChecksumMismatch()
    {
        Assert.Throws<InvalidDataException>(() =>
            new IndexedChebyshevEphemerisProvider(_datasetPath, expectedSha256: new string('0', 64)));
    }

    [Fact]
    public void Constructor_RejectsMissingFile()
    {
        Assert.Throws<FileNotFoundException>(() =>
            new IndexedChebyshevEphemerisProvider(Path.Combine(Path.GetTempPath(), "sn4_nope_missing.sn4bin")));
    }

    [Fact]
    public void Constructor_RejectsNegativeBudget()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            new IndexedChebyshevEphemerisProvider(_datasetPath, null, -1));
    }

    [Fact]
    public void Constructor_RejectsTruncatedFile()
    {
        string path = Path.Combine(Path.GetTempPath(), "sn4_trunc_" + Guid.NewGuid().ToString("N") + ".sn4bin");
        _tempFiles.Add(path);
        File.Copy(_datasetPath, path);
        using (var stream = new FileStream(path, FileMode.Open, FileAccess.Write, FileShare.Read))
            stream.SetLength(new FileInfo(path).Length / 2);
        Assert.Throws<InvalidDataException>(() =>
            new IndexedChebyshevEphemerisProvider(path, expectedSha256: null));
    }

    [Fact]
    public void ApplicationPath_RelativeCalculator_WorksOverLazyProvider()
    {
        // Exercises the exact provider interface the console application
        // uses (IEphemerisProvider + RelativePositionCalculator).
        using var provider = OpenDefault();
        var calc = new RelativePositionCalculator(provider);
        var epoch = new AstroTime(2460000.5, TimeScale.Tdb);
        var result = calc.Calculate("saturn", "earth", epoch);
        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.True(result.Value!.SeparationDistanceKm > 1.0e9);
        Assert.Contains("DE440s", result.Value.DataSource);
    }

    [Fact]
    public void Provider_MercuryVenusCenters_CoincideWithBarycenters()
    {
        // Measured NAIF convention (verified against independent spkezr):
        // DE440s stores Mercury center (199) exactly coincident with the
        // Mercury system barycenter (1), and Venus center (299) exactly
        // coincident with the Venus system barycenter (2) — spkezr returns
        // identically zero for 199-vs-1 and 299-vs-2 at all epochs. The
        // "_center" segments are genuine kernel data (single degree-1
        // records chained 199->1->0 / 299->2->0), not fabrications, and the
        // runtime preserves the identifier distinction.
        using var provider = OpenDefault();
        var epoch = new AstroTime(2451545.0 + 12.3456789, TimeScale.Tdb);
        foreach (var (center, bary) in new[] { ("mercury_center", "mercury"), ("venus_center", "venus") })
        {
            var baryState = provider.GetState(bary, epoch, ReferenceFrame.Barycentric);
            var centerState = provider.GetState(center, epoch, ReferenceFrame.Barycentric);
            Assert.True(baryState.IsSuccess && centerState.IsSuccess);
            double offsetKm = baryState.Value!.Position.DistanceTo(centerState.Value!.Position) / Distance.MetersPerKilometer;
            Assert.Equal(0.0, offsetKm);
            var rel = provider.GetRelativeState(center, bary, epoch);
            Assert.True(rel.IsSuccess);
            Assert.Equal(0.0, rel.Value!.Position.Magnitude);
            Assert.Equal(0.0, rel.Value!.Velocity.Magnitude);
        }
    }
}
