using System.Text.Json;
using System.Text.Json.Serialization;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// Independent scientific verification: the managed continuous provider is
/// compared against CSPICE spkezr references generated on the host from the
/// authenticated DE440s kernel (sn3_reference.json). The fixture was built
/// with only the high-level spkezr API — never raw Chebyshev records — so
/// this is a genuine two-path comparison.
///
/// Tolerances are implementation-comparison thresholds (managed evaluation
/// vs CSPICE on identical input epochs), NOT claims about absolute
/// astronomical accuracy:
///   position: 1e-6 km (1 mm) component-wise
///   velocity: 1e-9 km/s (1 nm/s) component-wise
/// Prototype validation showed ~1e-7 km / ~1e-14 km/s worst case, so these
/// limits carry roughly 10x / 1e5x margin for platform float differences.
/// </summary>
public class ChebyshevVerificationTests
{
    private const double PosTolKm = 1e-6;
    private const double VelTolKms = 1e-9;

    private readonly ITestOutputHelper _output;
    private readonly string _datasetPath;
    private readonly List<ReferenceCase> _cases;

    public ChebyshevVerificationTests(ITestOutputHelper output)
    {
        _output = output;
        _datasetPath = ChebyshevProviderTests.ResolveDatasetPath();
        var refPath = Path.Combine(AppContext.BaseDirectory, "sn3_reference.json");
        var fixture = JsonSerializer.Deserialize<ReferenceFixture>(File.ReadAllText(refPath))
            ?? throw new InvalidDataException("Failed to load sn3_reference.json");
        _cases = fixture.Cases;
    }

    [Fact]
    public void Fixture_ContainsAtLeast100OffGridCases()
    {
        int offgrid = _cases.Count(c => c.Case == "random_offgrid");
        Assert.True(offgrid >= 100, $"Only {offgrid} off-grid cases; at least 100 required.");
    }

    [Fact]
    public void ManagedProvider_MatchesCspice_AcrossAllCases()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        double maxPosErr = 0.0, sumPosSq = 0.0;
        double maxVelErr = 0.0, sumVelSq = 0.0;
        string worstPos = "", worstVel = "";
        int compared = 0, offgrid = 0;
        var failures = new List<string>();

        foreach (var c in _cases)
        {
            var epoch = new AstroTime(c.JdTdb, TimeScale.Tdb);
            NavigationResult<(Vector3D Position, Vector3D Velocity)> result =
                c.ObserverId == "ssb"
                    ? provider.GetState(c.BodyId, epoch, ReferenceFrame.Barycentric)
                    : provider.GetRelativeState(c.BodyId, c.ObserverId, epoch);

            if (!result.IsSuccess)
            {
                failures.Add($"{c.Case} {c.BodyId}/{c.ObserverId} et={c.EtSec}: provider failed: {result.ErrorMessage}");
                continue;
            }

            var (pos, vel) = result.Value!;
            double px = pos.X / Distance.MetersPerKilometer - c.PosKm[0];
            double py = pos.Y / Distance.MetersPerKilometer - c.PosKm[1];
            double pz = pos.Z / Distance.MetersPerKilometer - c.PosKm[2];
            double vx = vel.X / Distance.MetersPerKilometer - c.VelKms[0];
            double vy = vel.Y / Distance.MetersPerKilometer - c.VelKms[1];
            double vz = vel.Z / Distance.MetersPerKilometer - c.VelKms[2];
            double posErr = Math.Max(Math.Abs(px), Math.Max(Math.Abs(py), Math.Abs(pz)));
            double velErr = Math.Max(Math.Abs(vx), Math.Max(Math.Abs(vy), Math.Abs(vz)));

            maxPosErr = Math.Max(maxPosErr, posErr);
            maxVelErr = Math.Max(maxVelErr, velErr);
            if (posErr >= maxPosErr) worstPos = $"{c.Case} {c.BodyId}/{c.ObserverId} et={c.EtSec}";
            if (velErr >= maxVelErr) worstVel = $"{c.Case} {c.BodyId}/{c.ObserverId} et={c.EtSec}";
            sumPosSq += posErr * posErr;
            sumVelSq += velErr * velErr;
            compared++;
            if (c.Case == "random_offgrid") offgrid++;

            if (posErr > PosTolKm || velErr > VelTolKms)
                failures.Add($"{c.Case} {c.BodyId}/{c.ObserverId} et={c.EtSec}: posErr={posErr:E3} km velErr={velErr:E3} km/s");
        }

        double rmsPos = Math.Sqrt(sumPosSq / Math.Max(1, compared));
        double rmsVel = Math.Sqrt(sumVelSq / Math.Max(1, compared));
        _output.WriteLine($"compared={compared} offgrid={offgrid}");
        _output.WriteLine($"maxPosErr={maxPosErr:E3} km worst=({worstPos})");
        _output.WriteLine($"rmsPosErr={rmsPos:E3} km");
        _output.WriteLine($"maxVelErr={maxVelErr:E3} km/s worst=({worstVel})");
        _output.WriteLine($"rmsVelErr={rmsVel:E3} km/s");

        Assert.True(failures.Count == 0,
            $"Mismatches: {failures.Count}. First: {string.Join(" | ", failures.Take(5))}");
        Assert.True(offgrid >= 100, $"Only {offgrid} off-grid comparisons.");
    }

    [Fact]
    public void ManagedProvider_MatchesCspice_AtRecordBoundaries()
    {
        var provider = new ChebyshevEphemerisProvider(_datasetPath);
        var boundary = _cases.Where(c => c.Case == "boundary").ToList();
        Assert.True(boundary.Count > 0, "Fixture contains no boundary cases.");
        double maxPosErr = 0.0, maxVelErr = 0.0;
        foreach (var c in boundary)
        {
            var epoch = new AstroTime(c.JdTdb, TimeScale.Tdb);
            var result = provider.GetState(c.BodyId, epoch, ReferenceFrame.Barycentric);
            Assert.True(result.IsSuccess, $"{c.BodyId} et={c.EtSec}: {result.ErrorMessage}");
            var (pos, vel) = result.Value!;
            maxPosErr = Math.Max(maxPosErr, new[]
            {
                Math.Abs(pos.X / Distance.MetersPerKilometer - c.PosKm[0]),
                Math.Abs(pos.Y / Distance.MetersPerKilometer - c.PosKm[1]),
                Math.Abs(pos.Z / Distance.MetersPerKilometer - c.PosKm[2])
            }.Max());
            maxVelErr = Math.Max(maxVelErr, new[]
            {
                Math.Abs(vel.X / Distance.MetersPerKilometer - c.VelKms[0]),
                Math.Abs(vel.Y / Distance.MetersPerKilometer - c.VelKms[1]),
                Math.Abs(vel.Z / Distance.MetersPerKilometer - c.VelKms[2])
            }.Max());
        }
        _output.WriteLine($"boundary cases={boundary.Count} maxPosErr={maxPosErr:E3} km maxVelErr={maxVelErr:E3} km/s");
        Assert.True(maxPosErr <= PosTolKm, $"Boundary position error {maxPosErr:E3} km exceeds {PosTolKm:E0} km.");
        Assert.True(maxVelErr <= VelTolKms, $"Boundary velocity error {maxVelErr:E3} km/s exceeds {VelTolKms:E0} km/s.");
    }

    private sealed class ReferenceFixture
    {
        [JsonPropertyName("cases")]
        public List<ReferenceCase> Cases { get; set; } = new();
    }

    private sealed class ReferenceCase
    {
        [JsonPropertyName("case")]
        public string Case { get; set; } = "";

        [JsonPropertyName("bodyId")]
        public string BodyId { get; set; } = "";

        [JsonPropertyName("observerId")]
        public string ObserverId { get; set; } = "";

        [JsonPropertyName("jdTdb")]
        public double JdTdb { get; set; }

        [JsonPropertyName("etSec")]
        public double EtSec { get; set; }

        [JsonPropertyName("posKm")]
        public double[] PosKm { get; set; } = Array.Empty<double>();

        [JsonPropertyName("velKms")]
        public double[] VelKms { get; set; } = Array.Empty<double>();
    }
}
