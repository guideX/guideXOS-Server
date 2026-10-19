using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Tests;

public sealed class Sn8SceneTests
{
    [Fact]
    public void TrajectoryUsesSunRelativePropagationAndDE440SunPlacement_WithoutChangingPlanets()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var orientation = IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        var gm = SunGravitationalParameter.Load(GmAssetPath());
        var builder = new SceneBuilder(provider, orientation, gm);
        var baseline = builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        var scene = builder.Build(new SceneBuildRequest(new EtSeconds(0),
            IncludeSimulatedTrajectory: true, TrajectorySampleCount: 49));

        Assert.NotNull(scene.Trajectory);
        var model = scene.Trajectory!.Model;
        var vehicle = scene.FindObject("sn8-spacecraft")!;
        var sun = baseline.FindObject("sun")!.State;
        Assert.Equal("ssb", vehicle.State.OriginId);
        Assert.Equal(FrameOrientation.J2000, vehicle.State.Orientation);
        Assert.True(vehicle.State.IsSimulated);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, vehicle.State.Quality);
        Assert.NotEqual(NavigationResultStatus.ValidatedReference, vehicle.State.Quality);
        Assert.Equal(sun.Position + model.CurrentState.PositionMeters, vehicle.State.Position);
        Assert.Equal(sun.Velocity + model.CurrentState.VelocityMetersPerSecond, vehicle.State.Velocity);
        Assert.Contains("not a self-consistent N-body integration", vehicle.State.Provenance);
        Assert.InRange(scene.Trajectory.Samples.Count, 2, TrajectoryGenerator.DefaultMaximumSamples);
        Assert.Equal(scene.Trajectory.Samples.OrderBy(sample => sample.State.EtSeconds)
            .Select(sample => sample.State.EtSeconds),
            scene.Trajectory.Samples.Select(sample => sample.State.EtSeconds));
        foreach (var definition in SceneBuilder.Definitions)
        {
            var before = baseline.FindObject(definition.Id)!.State;
            var after = scene.FindObject(definition.Id)!.State;
            Assert.Equal(before.Position, after.Position);
            Assert.Equal(before.Velocity, after.Velocity);
            Assert.Equal(before.Quality, after.Quality);
            Assert.False(after.IsSimulated);
        }
    }

    [Fact]
    public void TrajectoryAndManeuverMarkersRespectEarthOriginAndExposeSelectableScientificDetails()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var gm = SunGravitationalParameter.Load(GmAssetPath());
        var builder = new SceneBuilder(provider, solarGm: gm);
        var scenario = SimulatedTrajectoryScenario.CreateDemonstration(gm);
        var maneuver = new ImpulsiveManeuver("SN8-TEST-1", 86400,
            new Vector3D(4.0, -2.0, 1.0), Label: "GUI acceptance event");
        var ssb = builder.Build(new SceneBuildRequest(new EtSeconds(0),
            IncludeSimulatedTrajectory: true, TrajectoryScenario: scenario,
            Maneuvers: [maneuver]));
        var earth = builder.Build(new SceneBuildRequest(new EtSeconds(0), SceneOriginMode.Earth,
            IncludeSimulatedTrajectory: true, TrajectoryScenario: scenario,
            Maneuvers: [maneuver]));
        var absolute = ssb.FindObject("sn8-spacecraft")!.State;
        var earthState = ssb.FindObject("earth")!.State;
        var relative = earth.FindObject("sn8-spacecraft")!.State;
        Assert.Equal(absolute.Position - earthState.Position, relative.Position);
        Assert.Equal(absolute.Velocity - earthState.Velocity, relative.Velocity);
        Assert.Equal("earth", relative.OriginId);
        Assert.Equal(FrameOrientation.J2000, relative.Orientation);

        var marker = Assert.Single(ssb.Trajectory!.ManeuverMarkers);
        Assert.Equal(maneuver.Id, marker.Id);
        Assert.Equal(maneuver.DeltaVelocityMetersPerSecond, marker.DeltaVelocityMetersPerSecond);
        Assert.Equal(marker.PreState.Position, marker.PostState.Position);
        Assert.Equal(maneuver.EtSeconds, marker.EtSeconds);
        var selected = ssb.FindObject($"maneuver:{maneuver.Id}")!;
        Assert.NotNull(selected.ManeuverSelection);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, selected.State.Quality);
        Assert.Contains(ssb.Trajectory.Samples, sample => sample.Kind == TrajectorySampleKind.ManeuverPre);
        Assert.Contains(ssb.Trajectory.Samples, sample => sample.Kind == TrajectorySampleKind.ManeuverPost);
    }

    [Fact]
    public void CameraAndSelectionRemainPresentationOnlyForTheModeledVehicle()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var gm = SunGravitationalParameter.Load(GmAssetPath());
        var builder = new SceneBuilder(provider, solarGm: gm);
        var scene = builder.Build(new SceneBuildRequest(new EtSeconds(0), IncludeSimulatedTrajectory: true));
        var vehicle = scene.FindObject("sn8-spacecraft")!;
        var position = vehicle.State.Position;
        var velocity = vehicle.State.Velocity;
        var camera = new Camera2D();
        var viewport = new Viewport(0, 0, 1000, 650);
        camera.Fit(scene.Objects.Select(item => item.State.Position)
            .Concat(scene.Trajectory!.Samples.Select(sample => sample.State.Position)), viewport);
        Assert.True(camera.TryWorldToScreen(position.X, position.Y, viewport, out var point));
        Assert.Equal("sn8-spacecraft", Camera2D.HitTest(scene, camera, point.X, point.Y,
            viewport, 1)?.Id);
        Assert.True(camera.PanPixels(120, -60));
        Assert.True(camera.ZoomAt(400, 220, 1.5, viewport));
        Assert.Equal(position, scene.FindObject("sn8-spacecraft")!.State.Position);
        Assert.Equal(velocity, scene.FindObject("sn8-spacecraft")!.State.Velocity);
    }

    [Fact]
    public void ProvisionalTimeRemainsStaleAndMissingGMNeverCreatesAFallbackTrajectory()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var gm = SunGravitationalParameter.Load(GmAssetPath());
        var builder = new SceneBuilder(provider, solarGm: gm);
        var stale = builder.Build(new SceneBuildRequest(new EtSeconds(0),
            TimeQuality: TimeConversionQuality.ProvisionalStaleTable,
            IncludeSimulatedTrajectory: true));
        Assert.Equal(NavigationResultStatus.StaleData, stale.Trajectory!.VehicleMarker.State.Quality);
        Assert.Equal(NavigationResultStatus.StaleData, stale.Trajectory.Model.Quality);

        var withoutGm = new SceneBuilder(provider);
        var exception = Assert.Throws<SceneBuildException>(() => withoutGm.Build(
            new SceneBuildRequest(new EtSeconds(0), IncludeSimulatedTrajectory: true)));
        Assert.Contains("No fallback gravitational parameter", exception.Message);
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
