using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 offline startup/application integration: the orientation asset ships
/// with the source tree (copied to App Data by StellarNavigator.App.csproj),
/// loads with integrity verification, and supports the exact framed queries
/// the console demonstration performs. No network, CSPICE, or Python is
/// touched by any of these paths.
/// </summary>
public class Sn6AppTests
{
    [Fact]
    public void OrientationAsset_ShipsWithSourceTree_AndLoads()
    {
        string path = Sn6Paths.OrientationAssetPath();
        Assert.True(File.Exists(path));
        var info = new FileInfo(path);
        Assert.True(info.Length > 1000 && info.Length < 100000);
        var model = IauEarthOrientation.Load(path);
        Assert.Contains("pck00011.tpc", model.Provenance);
    }

    [Fact]
    public void DemoQueries_SucceedOffline()
    {
        IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var epoch = new AstroTime(2451557.3456789, TimeScale.Tdb);
        Assert.True(svc.GetState("earth", epoch, FrameOrientation.J2000, "ssb").IsSuccess);
        Assert.True(svc.GetState("mars", epoch, FrameOrientation.J2000, "earth").IsSuccess);
        Assert.True(svc.GetState("mars", epoch, FrameOrientation.IauEarth, "earth").IsSuccess);
        var sc = new SpacecraftState
        {
            SpacecraftId = "demo-sim-1",
            Position = new Vector3D(0, 0, 0),
            Velocity = new Vector3D(0, 0, 0),
            Frame = ReferenceFrame.Barycentric,
            CenterOfReferenceId = "ssb",
            Epoch = epoch,
            Provenance = StateProvenance.Simulated,
        };
        var rel = svc.GetSpacecraftRelative("jupiter", sc);
        Assert.True(rel.IsSuccess, rel.ErrorMessage);
        Assert.False(svc.GetState("mars", epoch, "ITRF93", "ssb").IsSuccess);
    }
}
