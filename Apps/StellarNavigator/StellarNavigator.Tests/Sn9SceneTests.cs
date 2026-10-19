using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Tests;

public sealed class Sn9SceneTests
{
    [Fact]
    public void SceneBuild_ExposesTargetSpecificTimelineAndRecomputesAfterManeuverEdit()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var gm = SunGravitationalParameter.Load(GmAssetPath());
        var builder = new SceneBuilder(provider, solarGm: gm);
        var scenario = SimulatedTrajectoryScenario.CreateDemonstration(gm);
        var request = new SceneBuildRequest(new EtSeconds(0), IncludeSimulatedTrajectory: true,
            TrajectoryScenario: scenario, TrajectorySampleCount: 25,
            TrajectoryWindowHalfSpanSeconds: 7 * 86_400.0,
            TrajectoryAnalysisTargetBodyId: "earth");

        var baseline = builder.Build(request);
        var maneuver = new ImpulsiveManeuver("SN9-UI-1", 86_400,
            new Vector3D(0, 250, 0), Label: "test transfer impulse");
        var maneuverScene = builder.Build(request with
        {
            Maneuvers = [maneuver],
            TrajectoryAnalysisTargetBodyId = "mars",
        });

        var baselineTimeline = baseline.Trajectory!.MissionEvents!;
        var maneuverTimeline = maneuverScene.Trajectory!.MissionEvents!;
        Assert.Equal("earth", baselineTimeline.TargetBodyId);
        Assert.Equal("mars", maneuverTimeline.TargetBodyId);
        Assert.Contains(baselineTimeline.Events, item => item.Type == MissionEventType.SimulationStart);
        Assert.Contains(baselineTimeline.Events, item => item.Type == MissionEventType.SimulationEnd);
        Assert.DoesNotContain(baselineTimeline.Events, item => item.Type == MissionEventType.Maneuver);
        var execution = Assert.Single(maneuverTimeline.Events,
            item => item.Type == MissionEventType.Maneuver);
        Assert.Equal("Applied", execution.ManeuverStatus);
        Assert.Equal(maneuver.Id, execution.ObjectId);
        Assert.All(maneuverTimeline.Events.Where(item => item.Type == MissionEventType.ClosestApproach),
            item => Assert.Equal("mars", item.ObjectId));
        Assert.NotSame(baselineTimeline.Events, maneuverTimeline.Events);
        Assert.True(maneuverTimeline.Events.Count <= MissionEventAnalyzer.MaximumEvents);
    }

    private static string GmAssetPath()
    {
        string output = Path.Combine(AppContext.BaseDirectory, "Data", SunGravitationalParameter.RuntimeAssetName);
        if (File.Exists(output))
            return output;
        string source = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..",
            "Navigation.Core", "Data", SunGravitationalParameter.RuntimeAssetName);
        if (File.Exists(source))
            return Path.GetFullPath(source);
        throw new FileNotFoundException("Authenticated SN8 solar GM test asset was not found.");
    }
}
