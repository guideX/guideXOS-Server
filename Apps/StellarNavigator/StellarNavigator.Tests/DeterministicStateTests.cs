using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class DeterministicStateTests : IDisposable
{
    private readonly string _tempDir;

    public DeterministicStateTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sn1_det_" + Guid.NewGuid().ToString("N"));
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
              "velocity": { "x": -0.0172, "y": -0.0031, "z": 0.0000 } }
          ]
        }
        """);
        return new PrecomputedEphemerisProvider(path);
    }

    [Fact]
    public void Ephemeris_SameInputSameOutput()
    {
        var provider = CreateProvider();
        var epoch = AstroTime.FromJ2000Days(0);

        var r1 = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);
        var r2 = provider.GetState("earth", epoch, ReferenceFrame.Heliocentric);

        Assert.Equal(r1.Value!.Item1.X, r2.Value!.Item1.X, 10);
        Assert.Equal(r1.Value!.Item1.Y, r2.Value!.Item1.Y, 10);
        Assert.Equal(r1.Value!.Item1.Z, r2.Value!.Item1.Z, 10);
    }

    [Fact]
    public void SpacecraftState_DeterministicConstruction()
    {
        var state1 = new SpacecraftState
        {
            SpacecraftId = "sim",
            Position = new Vector3D(1, 2, 3),
            Velocity = new Vector3D(4, 5, 6),
            Frame = ReferenceFrame.Heliocentric,
            CenterOfReferenceId = "sun",
            Epoch = AstroTime.FromJ2000Days(0),
            Provenance = StateProvenance.Simulated
        };

        var state2 = new SpacecraftState
        {
            SpacecraftId = "sim",
            Position = new Vector3D(1, 2, 3),
            Velocity = new Vector3D(4, 5, 6),
            Frame = ReferenceFrame.Heliocentric,
            CenterOfReferenceId = "sun",
            Epoch = AstroTime.FromJ2000Days(0),
            Provenance = StateProvenance.Simulated
        };

        Assert.Equal(state1.Position, state2.Position);
        Assert.Equal(state1.Velocity, state2.Velocity);
        Assert.Equal(state1.Epoch.JdUtc, state2.Epoch.JdUtc, 10);
    }

    [Fact]
    public void ReferenceFrame_Equality()
    {
        var f1 = ReferenceFrame.Heliocentric;
        var f2 = ReferenceFrame.Heliocentric;
        Assert.Equal(f1, f2);
    }

    [Fact]
    public void ReferenceFrame_Inequality()
    {
        var f1 = ReferenceFrame.Heliocentric;
        var f2 = ReferenceFrame.IcrfJ2000;
        Assert.NotEqual(f1, f2);
    }
}
