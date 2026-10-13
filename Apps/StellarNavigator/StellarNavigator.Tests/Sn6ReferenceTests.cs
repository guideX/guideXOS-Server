using System.Text.Json;
using Astronomy.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>
/// Independent SN6 verification: the managed J2000 &lt;-&gt; IAU_EARTH transform is
/// compared against CSPICE pxform/sxform references (N0067, authenticated
/// pck00011.tpc) generated on the host by tools/sn6_make_reference.py —
/// never from the implementation under test.
///
/// Fixture: 113 epochs (J2000, historical, modern, rotation phases,
/// coverage boundaries, 100 deterministic off-grid) x 4 representative
/// vectors = 452 state-transform comparisons (400 off-grid).
///
/// Tolerances are representation-aware, NOT weakened to pass: R/Rdot use
/// absolute bounds sized by binary64 quantization of the fast Earth-rotation
/// phase W (error grows with |ET|); position uses a relative bound and
/// velocity a magnitude-scaled bound. Any formula-level bug (wrong Euler
/// order, transposed product, missing R' coupling, degree/radian slip)
/// produces discrepancies orders of magnitude above these bounds.
/// Matching CSPICE validates the implementation, not flight readiness.
/// </summary>
public class Sn6ReferenceTests
{
    // Absolute: binary64 W-phase quantization reaches ~8e-11 rad at the
    // coverage boundaries (|W| ~ 2e7 deg there); near J2000 agreement is ~1e-16.
    private const double RotAbsTol = 2e-10;
    // Rdot entries are O(7.3e-5 /s); error scales as Wdot * R-error.
    private const double RotDotAbsTol = 3e-14;
    // Position: relative to |r| (zero-vector floor 1e-6 m). Sized by the same
    // W-phase quantization: |W|~2e7 deg at the boundaries quantizes to
    // ~4e-9 deg (~8e-11 rad), i.e. ~1e-10 relative on rotated positions.
    private const double PosRelTol = 1e-10;
    private const double PosAbsFloor = 1e-6;
    // Velocity: scaled by |v| and the Rdot*r coupling magnitude.
    private const double VelTolA = 1e-9;
    private const double VelTolB = 3e-14;
    private const double VelAbsFloor = 1e-9;

    private readonly ITestOutputHelper _output;
    private readonly Sn6Fixture _fixture;

    public Sn6ReferenceTests(ITestOutputHelper output)
    {
        _output = output;
        var json = File.ReadAllText(Sn6Paths.Sn6ReferencePath());
        _fixture = JsonSerializer.Deserialize<Sn6Fixture>(json,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
            ?? throw new InvalidDataException("Failed to load sn6_reference.json");
    }

    [Fact]
    public void Fixture_MeetsCoverageMinimums()
    {
        int offgrid = _fixture.Cases.Count(c => c.Tag == "offgrid");
        int comparisons = _fixture.Cases.Sum(c => c.States.Count);
        int offgridComparisons = _fixture.Cases
            .Where(c => c.Tag == "offgrid").Sum(c => c.States.Count);
        Assert.True(_fixture.Cases.Count >= 110, $"epochs: {_fixture.Cases.Count}");
        Assert.True(offgrid >= 100, $"off-grid epochs: {offgrid}");
        Assert.True(comparisons >= 150, $"comparisons: {comparisons}");
        Assert.True(offgridComparisons >= 100, $"off-grid comparisons: {offgridComparisons}");
        Assert.Contains(_fixture.Cases, c => c.Tag == "j2000");
        Assert.Contains(_fixture.Cases, c => c.Tag == "historical");
        Assert.Contains(_fixture.Cases, c => c.Tag == "modern");
        Assert.Contains(_fixture.Cases, c => c.Tag == "rotation_phase");
        Assert.Contains(_fixture.Cases, c => c.Tag == "boundary");
        _output.WriteLine(
            $"epochs={_fixture.Cases.Count} offgrid={offgrid} comparisons={comparisons} offgridComparisons={offgridComparisons}");
    }

    [Fact]
    public void Rotation_MatchesCspice()
    {
        double maxAbs = 0, sumSq = 0;
        long n = 0;
        double worstEt = 0;
        foreach (var c in _fixture.Cases)
        {
            IauEarthOrientation.GetRotation(c.Et, out var r, out _);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                {
                    double d = Math.Abs(r[i, j] - c.R[i][j]);
                    sumSq += d * d; n++;
                    if (d > maxAbs) { maxAbs = d; worstEt = c.Et; }
                }
        }
        double rms = Math.Sqrt(sumSq / n);
        _output.WriteLine($"R: maxAbs={maxAbs:E3} rms={rms:E3} worstEt={worstEt:E3} (tol {RotAbsTol:E0})");
        Assert.True(maxAbs <= RotAbsTol, $"R maxAbs {maxAbs:E3} exceeds {RotAbsTol:E0}");
    }

    [Fact]
    public void RotationDerivative_MatchesCspice()
    {
        double maxAbs = 0, sumSq = 0;
        long n = 0;
        double worstEt = 0;
        foreach (var c in _fixture.Cases)
        {
            IauEarthOrientation.GetRotation(c.Et, out _, out var rd);
            double[,] got = { { rd.D00, rd.D01, rd.D02 }, { rd.D10, rd.D11, rd.D12 }, { rd.D20, rd.D21, rd.D22 } };
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                {
                    double d = Math.Abs(got[i, j] - c.Rdot[i][j]);
                    sumSq += d * d; n++;
                    if (d > maxAbs) { maxAbs = d; worstEt = c.Et; }
                }
        }
        double rms = Math.Sqrt(sumSq / n);
        _output.WriteLine($"Rdot: maxAbs={maxAbs:E3} rms={rms:E3} worstEt={worstEt:E3} (tol {RotDotAbsTol:E0})");
        Assert.True(maxAbs <= RotDotAbsTol, $"Rdot maxAbs {maxAbs:E3} exceeds {RotDotAbsTol:E0}");
    }

    [Fact]
    public void StateTransform_MatchesCspice()
    {
        double maxPosAbs = 0, maxPosRel = 0, posSumSq = 0;
        double maxVelAbs = 0, velSumSq = 0;
        long posN = 0, velN = 0;
        string worstPos = "", worstVel = "";
        int count = 0;
        foreach (var c in _fixture.Cases)
        {
            IauEarthOrientation.GetRotation(c.Et, out var r, out var rd);
            foreach (var s in c.States)
            {
                count++;
                var pos = new Vector3D(s.RJ2000[0], s.RJ2000[1], s.RJ2000[2]);
                var vel = new Vector3D(s.VJ2000[0], s.VJ2000[1], s.VJ2000[2]);
                var (p, v) = StateTransformMath.TransformState(r, rd, pos, vel);
                double[] ep = { p.X - s.REarth[0], p.Y - s.REarth[1], p.Z - s.REarth[2] };
                double[] ev = { v.X - s.VEarth[0], v.Y - s.VEarth[1], v.Z - s.VEarth[2] };
                double rMag = pos.Magnitude, vMag = vel.Magnitude;
                double posAbs = Math.Sqrt(ep[0] * ep[0] + ep[1] * ep[1] + ep[2] * ep[2]);
                double velAbs = Math.Sqrt(ev[0] * ev[0] + ev[1] * ev[1] + ev[2] * ev[2]);
                double posTol = Math.Max(PosAbsFloor, PosRelTol * rMag);
                double velTol = VelTolA * vMag + VelTolB * rMag + VelAbsFloor;
                posSumSq += posAbs * posAbs; posN++;
                velSumSq += velAbs * velAbs; velN++;
                if (posAbs > maxPosAbs) { maxPosAbs = posAbs; worstPos = $"et={c.Et:E3} {s.Tag}"; }
                if (rMag > 0) maxPosRel = Math.Max(maxPosRel, posAbs / rMag);
                if (velAbs > maxVelAbs) { maxVelAbs = velAbs; worstVel = $"et={c.Et:E3} {s.Tag}"; }
                Assert.True(posAbs <= posTol,
                    $"pos et={c.Et} {s.Tag}: {posAbs:E3} > tol {posTol:E3}");
                Assert.True(velAbs <= velTol,
                    $"vel et={c.Et} {s.Tag}: {velAbs:E3} > tol {velTol:E3}");
            }
        }
        _output.WriteLine($"states: {count}");
        _output.WriteLine($"pos: maxAbs={maxPosAbs:E3} maxRel={maxPosRel:E3} rms={Math.Sqrt(posSumSq / posN):E3} worst=[{worstPos}]");
        _output.WriteLine($"vel: maxAbs={maxVelAbs:E3} rms={Math.Sqrt(velSumSq / velN):E3} worst=[{worstVel}]");
    }

    [Fact]
    public void InverseTransform_MatchesCspiceInverse()
    {
        // Managed inverse applied to the INDEPENDENT CSPICE forward output
        // must recover the original J2000 input (tests the inverse law,
        // including velocity coupling, against the reference implementation).
        double maxPosRel = 0, maxVelAbs = 0;
        int count = 0;
        foreach (var c in _fixture.Cases)
        {
            IauEarthOrientation.GetRotation(c.Et, out var r, out var rd);
            foreach (var s in c.States)
            {
                count++;
                var pd = new Vector3D(s.REarth[0], s.REarth[1], s.REarth[2]);
                var vd = new Vector3D(s.VEarth[0], s.VEarth[1], s.VEarth[2]);
                var (p, v) = StateTransformMath.InverseTransformState(r, rd, pd, vd);
                double rMag = new Vector3D(s.RJ2000[0], s.RJ2000[1], s.RJ2000[2]).Magnitude;
                double vMag = new Vector3D(s.VJ2000[0], s.VJ2000[1], s.VJ2000[2]).Magnitude;
                double posAbs = Math.Sqrt(
                    (p.X - s.RJ2000[0]) * (p.X - s.RJ2000[0]) +
                    (p.Y - s.RJ2000[1]) * (p.Y - s.RJ2000[1]) +
                    (p.Z - s.RJ2000[2]) * (p.Z - s.RJ2000[2]));
                double velAbs = Math.Sqrt(
                    (v.X - s.VJ2000[0]) * (v.X - s.VJ2000[0]) +
                    (v.Y - s.VJ2000[1]) * (v.Y - s.VJ2000[1]) +
                    (v.Z - s.VJ2000[2]) * (v.Z - s.VJ2000[2]));
                if (rMag > 0) maxPosRel = Math.Max(maxPosRel, posAbs / rMag);
                maxVelAbs = Math.Max(maxVelAbs, velAbs);
                Assert.True(posAbs <= Math.Max(PosAbsFloor, PosRelTol * rMag),
                    $"inv pos et={c.Et} {s.Tag}: {posAbs:E3}");
                Assert.True(velAbs <= VelTolA * vMag + VelTolB * rMag + VelAbsFloor,
                    $"inv vel et={c.Et} {s.Tag}: {velAbs:E3}");
            }
        }
        _output.WriteLine($"inverse vs CSPICE: {count} cases, maxPosRel={maxPosRel:E3} maxVelAbs={maxVelAbs:E3}");
    }

    [Fact]
    public void RoundTrip_J2000_IauEarth_J2000()
    {
        int count = 0;
        foreach (var c in _fixture.Cases)
        {
            IauEarthOrientation.GetRotation(c.Et, out var r, out var rd);
            foreach (var s in c.States)
            {
                count++;
                var pos = new Vector3D(s.RJ2000[0], s.RJ2000[1], s.RJ2000[2]);
                var vel = new Vector3D(s.VJ2000[0], s.VJ2000[1], s.VJ2000[2]);
                var (p1, v1) = StateTransformMath.TransformState(r, rd, pos, vel);
                var (p2, v2) = StateTransformMath.InverseTransformState(r, rd, p1, v1);
                double rMag = pos.Magnitude, vMag = vel.Magnitude;
                double posAbs = (p2 - pos).Magnitude;
                double velAbs = (v2 - vel).Magnitude;
                Assert.True(posAbs <= Math.Max(1e-9, 1e-12 * rMag), $"roundtrip pos et={c.Et}: {posAbs:E3}");
                Assert.True(velAbs <= 1e-9 * vMag + 1e-15 * rMag + 1e-9, $"roundtrip vel et={c.Et}: {velAbs:E3}");
            }
        }
        _output.WriteLine($"round-trip: {count} cases OK");
    }

    public sealed class Sn6Fixture
    {
        public Sn6Meta Metadata { get; set; } = new();
        public List<Sn6Case> Cases { get; set; } = new();
    }

    public sealed class Sn6Meta
    {
        public string PckSha256 { get; set; } = "";
        public int StateComparisons { get; set; }
        public int OffgridComparisons { get; set; }
    }

    public sealed class Sn6Case
    {
        public double Et { get; set; }
        public string Tag { get; set; } = "";
        public double[][] R { get; set; } = Array.Empty<double[]>();
        public double[][] Rdot { get; set; } = Array.Empty<double[]>();
        public List<Sn6Vec> States { get; set; } = new();
    }

    public sealed class Sn6Vec
    {
        public string Tag { get; set; } = "";
        public double[] RJ2000 { get; set; } = Array.Empty<double>();
        public double[] VJ2000 { get; set; } = Array.Empty<double>();
        public double[] REarth { get; set; } = Array.Empty<double>();
        public double[] VEarth { get; set; } = Array.Empty<double>();
    }
}
