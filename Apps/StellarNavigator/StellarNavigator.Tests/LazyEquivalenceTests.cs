using System.Text.Json;
using System.Text.Json.Serialization;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// Lazy-loader equivalence: for identical datasets and epochs, the indexed
/// provider must agree with the accepted SN3 full-load provider. Because
/// both evaluate identical verbatim coefficient doubles through the same
/// <see cref="ChebyshevEvaluator"/> path, agreement is asserted bit-exactly.
/// Cache history (hits, misses, evictions, budget exhaustion, restarts) must
/// not change scientific results.
/// </summary>
public class LazyEquivalenceTests
{
    private static readonly string[] SharedBodies =
        { "sun", "emb", "moon", "earth", "mars", "jupiter" };

    private readonly ITestOutputHelper _output;
    private readonly string _jsonPath;
    private readonly string _binPath;

    public LazyEquivalenceTests(ITestOutputHelper output)
    {
        _output = output;
        _jsonPath = StellarNavigator.Tests.ChebyshevProviderTests.ResolveDatasetPath();
        _binPath = Sn4Paths.DatasetPath();
    }

    [Fact]
    public void LazyProvider_BitExactlyMatchesSn3Provider_AtDeterministicEpochs()
    {
        var full = new ChebyshevEphemerisProvider(_jsonPath);
        using var lazy = new IndexedChebyshevEphemerisProvider(_binPath);
        double[] jdOffsets =
        {
            0.0, 12.3456789, -1000.5, 3333.333, 50000.123456, -54000.0, 54000.0,
            77.777, 3.14159265, 12345.6789,
        };
        int compared = 0;
        foreach (var body in SharedBodies)
        {
            foreach (var offset in jdOffsets)
            {
                var epoch = new AstroTime(2451545.0 + offset, TimeScale.Tdb);
                var a = full.GetState(body, epoch, ReferenceFrame.Barycentric);
                var b = lazy.GetState(body, epoch, ReferenceFrame.Barycentric);
                Assert.True(a.IsSuccess, $"full {body}: {a.ErrorMessage}");
                Assert.True(b.IsSuccess, $"lazy {body}: {b.ErrorMessage}");
                Assert.Equal(a.Value!.Position.X, b.Value!.Position.X);
                Assert.Equal(a.Value!.Position.Y, b.Value!.Position.Y);
                Assert.Equal(a.Value!.Position.Z, b.Value!.Position.Z);
                Assert.Equal(a.Value!.Velocity.X, b.Value!.Velocity.X);
                Assert.Equal(a.Value!.Velocity.Y, b.Value!.Velocity.Y);
                Assert.Equal(a.Value!.Velocity.Z, b.Value!.Velocity.Z);
                compared++;
            }
        }
        _output.WriteLine($"bit-exact comparisons={compared}");
    }

    [Fact]
    public void LazyProvider_BitExactlyMatchesSn3Provider_AtRandomEpochs()
    {
        var full = new ChebyshevEphemerisProvider(_jsonPath);
        using var lazy = new IndexedChebyshevEphemerisProvider(_binPath);
        var rng = new Random(20261013);
        int compared = 0;
        for (int i = 0; i < 600; i++)
        {
            string body = SharedBodies[rng.Next(SharedBodies.Length)];
            double jd = 2396752.5 + rng.NextDouble() * (2506352.5 - 2396752.5);
            var epoch = new AstroTime(jd, TimeScale.Tdb);
            var a = full.GetState(body, epoch, ReferenceFrame.Barycentric);
            var b = lazy.GetState(body, epoch, ReferenceFrame.Barycentric);
            Assert.True(a.IsSuccess && b.IsSuccess, $"{body} jd={jd}");
            Assert.Equal(a.Value!.Position.X, b.Value!.Position.X);
            Assert.Equal(a.Value!.Position.Y, b.Value!.Position.Y);
            Assert.Equal(a.Value!.Position.Z, b.Value!.Position.Z);
            Assert.Equal(a.Value!.Velocity.X, b.Value!.Velocity.X);
            Assert.Equal(a.Value!.Velocity.Y, b.Value!.Velocity.Y);
            Assert.Equal(a.Value!.Velocity.Z, b.Value!.Velocity.Z);
            compared++;
        }
        _output.WriteLine($"random bit-exact comparisons={compared}");
    }

    [Fact]
    public void LazyProvider_CachePressure_DoesNotChangeResults()
    {
        var full = new ChebyshevEphemerisProvider(_jsonPath);
        // Tiny budget forces constant eviction across alternating targets.
        using var lazy = new IndexedChebyshevEphemerisProvider(_binPath, cacheBudgetBytes: 2048);
        var rng = new Random(4242);
        for (int i = 0; i < 300; i++)
        {
            string body = SharedBodies[rng.Next(SharedBodies.Length)];
            double jd = 2396752.5 + rng.NextDouble() * (2506352.5 - 2396752.5);
            var epoch = new AstroTime(jd, TimeScale.Tdb);
            var a = full.GetState(body, epoch, ReferenceFrame.Barycentric);
            var b = lazy.GetState(body, epoch, ReferenceFrame.Barycentric);
            Assert.True(a.IsSuccess && b.IsSuccess);
            Assert.Equal(a.Value!.Position.X, b.Value!.Position.X);
            Assert.Equal(a.Value!.Velocity.Z, b.Value!.Velocity.Z);
            Assert.True(lazy.CachedBytes <= lazy.CacheBudgetBytes);
        }
    }

    [Fact]
    public void LazyProvider_AgreesWithSn3Fixture_OnSharedBodies()
    {
        // The retained SN3 CSPICE fixture re-run through the lazy path:
        // guards numerical regressions on SN1–SN3 bodies via the new loader.
        using var lazy = new IndexedChebyshevEphemerisProvider(_binPath);
        var refPath = Path.Combine(AppContext.BaseDirectory, "sn3_reference.json");
        var fixture = JsonSerializer.Deserialize<Sn3Fixture>(File.ReadAllText(refPath))
            ?? throw new InvalidDataException("Failed to load sn3_reference.json");
        const double posTolKm = 1e-6;
        const double velTolKms = 1e-9;
        double maxPosErr = 0.0, maxVelErr = 0.0;
        int compared = 0;
        var failures = new List<string>();
        foreach (var c in fixture.Cases)
        {
            var epoch = new AstroTime(c.JdTdb, TimeScale.Tdb);
            NavigationResult<(Vector3D Position, Vector3D Velocity)> result =
                c.ObserverId == "ssb"
                    ? lazy.GetState(c.BodyId, epoch, ReferenceFrame.Barycentric)
                    : lazy.GetRelativeState(c.BodyId, c.ObserverId, epoch);
            if (!result.IsSuccess)
            {
                failures.Add($"{c.Case} {c.BodyId}: {result.ErrorMessage}");
                continue;
            }
            var (pos, vel) = result.Value!;
            double posErr = Math.Max(
                Math.Abs(pos.X / Distance.MetersPerKilometer - c.PosKm[0]),
                Math.Max(
                    Math.Abs(pos.Y / Distance.MetersPerKilometer - c.PosKm[1]),
                    Math.Abs(pos.Z / Distance.MetersPerKilometer - c.PosKm[2])));
            double velErr = Math.Max(
                Math.Abs(vel.X / Distance.MetersPerKilometer - c.VelKms[0]),
                Math.Max(
                    Math.Abs(vel.Y / Distance.MetersPerKilometer - c.VelKms[1]),
                    Math.Abs(vel.Z / Distance.MetersPerKilometer - c.VelKms[2])));
            maxPosErr = Math.Max(maxPosErr, posErr);
            maxVelErr = Math.Max(maxVelErr, velErr);
            compared++;
            if (posErr > posTolKm || velErr > velTolKms)
                failures.Add($"{c.Case} {c.BodyId}: posErr={posErr:E3} velErr={velErr:E3}");
        }
        _output.WriteLine($"sn3-fixture via lazy: compared={compared} maxPosErr={maxPosErr:E3} maxVelErr={maxVelErr:E3}");
        Assert.True(failures.Count == 0, string.Join(" | ", failures.Take(5)));
    }

    private sealed class Sn3Fixture
    {
        [JsonPropertyName("cases")]
        public List<Sn3Case> Cases { get; set; } = new();
    }

    private sealed class Sn3Case
    {
        [JsonPropertyName("case")]
        public string Case { get; set; } = "";

        [JsonPropertyName("bodyId")]
        public string BodyId { get; set; } = "";

        [JsonPropertyName("observerId")]
        public string ObserverId { get; set; } = "";

        [JsonPropertyName("jdTdb")]
        public double JdTdb { get; set; }

        [JsonPropertyName("posKm")]
        public double[] PosKm { get; set; } = Array.Empty<double>();

        [JsonPropertyName("velKms")]
        public double[] VelKms { get; set; } = Array.Empty<double>();
    }
}
