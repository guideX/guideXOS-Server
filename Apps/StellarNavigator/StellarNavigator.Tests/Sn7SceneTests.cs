using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Tests;

public sealed class Sn7Fixture : IDisposable
{
    public IndexedChebyshevEphemerisProvider Provider { get; }
    public IauEarthOrientation Orientation { get; }
    public SceneBuilder Builder { get; }

    public Sn7Fixture()
    {
        Provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        Orientation = IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        Builder = new SceneBuilder(Provider, Orientation);
    }

    public void Dispose() => Provider.Dispose();
}

public sealed class Sn7SceneTests(Sn7Fixture fixture) : IClassFixture<Sn7Fixture>
{
    [Fact]
    public void SceneBuild_UsesRealDe440sStates_ForSunAndEightPlanets()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        string[] planets = ["mercury", "venus", "earth", "mars", "jupiter", "saturn", "uranus", "neptune"];
        Assert.All(planets, id => Assert.NotNull(scene.FindObject(id)));
        Assert.NotNull(scene.FindObject("sun"));
        Assert.NotNull(scene.FindObject("moon"));
        Assert.All(scene.Objects.Where(x => x.Definition is not null), item =>
        {
            Assert.Equal(NavigationResultStatus.ValidatedReference, item.State.Quality);
            Assert.Equal(FrameOrientation.J2000, item.State.Orientation);
            Assert.Equal("ssb", item.State.OriginId);
            Assert.Contains("DE440s", item.State.Provenance);
            Assert.True(double.IsFinite(item.State.Position.X));
            Assert.True(double.IsFinite(item.State.Position.Y));
            Assert.True(double.IsFinite(item.State.Position.Z));
        });
    }

    [Fact]
    public void SceneDefinitions_RetainExactNaifAndBarycenterIdentities()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0), IncludeMoon: true));
        int?[] ids = [10, 1, 2, 399, 4, 5, 6, 7, 8, 301];
        string[] names = ["sun", "mercury", "venus", "earth", "mars", "jupiter", "saturn", "uranus", "neptune", "moon"];
        Assert.Equal(ids, names.Select(name => scene.FindObject(name)!.NaifId));
        Assert.Contains("system barycenter", scene.FindObject("mars")!.CenterClassification);
        Assert.Contains("system barycenter", scene.FindObject("jupiter")!.CenterClassification);
        Assert.Contains("geocenter", scene.FindObject("earth")!.CenterClassification);
        Assert.Contains("Body center", scene.FindObject("sun")!.CenterClassification);
    }

    [Fact]
    public void EarthMode_UsesEarthGeocenterAsOrigin_WithoutChangingAxesMetadata()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(123456789), SceneOriginMode.Earth));
        Assert.Equal("earth", scene.ScientificOriginId);
        Assert.All(scene.Objects, item => Assert.Equal("earth", item.State.OriginId));
        Assert.Equal(new Vector3D(0, 0, 0), scene.FindObject("earth")!.State.Position);
        Assert.Equal(FrameOrientation.J2000, scene.FindObject("mars")!.State.Orientation);
        Assert.Contains("earth", scene.FindObject("mars")!.State.Provenance);
        Assert.Contains("origin-translated", scene.FindObject("mars")!.State.Provenance);
    }

    [Fact]
    public void IauEarthView_RequiresAuthenticatedOrientation_AndLabelsModelLimitations()
    {
        var missing = new SceneBuilder(fixture.Provider);
        var rejected = Assert.Throws<SceneBuildException>(() => missing.Build(new SceneBuildRequest(
            new EtSeconds(0), SceneOriginMode.Earth, FrameOrientation.IauEarth)));
        Assert.Contains("authenticated", rejected.Message);

        var scene = fixture.Builder.Build(new SceneBuildRequest(
            new EtSeconds(0), SceneOriginMode.Earth, FrameOrientation.IauEarth));
        Assert.Equal(FrameOrientation.IauEarth, scene.FindObject("earth")!.State.Orientation);
        Assert.Contains("pck00011.tpc", scene.FindObject("mars")!.State.Provenance);
    }

    [Fact]
    public void SpacecraftOrigin_IsAnExplicitSimulation_AndDoesNotDependOnCamera()
    {
        var ssb = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        var camera = new Camera2D();
        camera.SetCenter(5e12, -3e11);
        camera.SetScale(3e-8);
        var earthBefore = ssb.FindObject("earth")!.State.Position;
        var spacecraftScene = fixture.Builder.Build(new SceneBuildRequest(
            new EtSeconds(0), SceneOriginMode.SimulatedSpacecraft));
        var spacecraft = spacecraftScene.FindObject("spacecraft")!;

        Assert.Equal("spacecraft:SN7-DEMO", spacecraftScene.ScientificOriginId);
        Assert.All(spacecraftScene.Objects, x => Assert.Equal(spacecraftScene.ScientificOriginId, x.State.OriginId));
        Assert.Equal(new Vector3D(0, 0, 0), spacecraft.State.Position);
        Assert.True(spacecraft.IsSimulated);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, spacecraft.State.Quality);
        Assert.Contains("not real spacecraft telemetry", spacecraft.State.Provenance);
        Assert.Equal(earthBefore, ssb.FindObject("earth")!.State.Position);
        Assert.Equal(5e12, camera.CenterX);
    }

    [Fact]
    public void ProvisionalUtc_DowngradesSceneQuality_AndKeepsSimulationFlagSeparate()
    {
        var provider = Sn5Paths.LoadProvider();
        var resolver = new EpochInputResolver(new TimeConverter(provider.Table, provider.Identity));
        var result = resolver.Resolve(EpochInputKind.Utc, "2026-10-18T12:00:00Z", allowProvisionalUtc: true);
        Assert.True(result.Success);
        Assert.Equal(TimeConversionQuality.ProvisionalStaleTable, result.TimeQuality);
        var scene = fixture.Builder.Build(new SceneBuildRequest(result.Epoch,
            TimeQuality: result.TimeQuality, EpochRepresentation: result.RepresentationPrecision));
        Assert.All(scene.Objects, item => Assert.Equal(NavigationResultStatus.StaleData, item.State.Quality));
        Assert.True(scene.FindObject("spacecraft")!.State.IsSimulated);
        Assert.Contains("ProvisionalStaleTable", scene.FindObject("earth")!.State.Provenance);
    }

    [Fact]
    public void UtcAfterAnnouncements_IsRefusedWithoutExplicitOptIn()
    {
        var provider = Sn5Paths.LoadProvider();
        var resolver = new EpochInputResolver(new TimeConverter(provider.Table, provider.Identity));
        var result = resolver.Resolve(EpochInputKind.Utc, "2026-10-18T12:00:00Z");
        Assert.False(result.Success);
        Assert.Equal(TimeConversionQuality.UnsupportedFuture, result.TimeQuality);
        Assert.Contains("refused", result.Error, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void EpochInput_PreservesEtAndMakesJdPrecisionExplicit()
    {
        var resolver = new EpochInputResolver(null);
        const string etText = "521726468.3071998";
        var et = resolver.Resolve(EpochInputKind.EtSeconds, etText);
        Assert.True(et.Success);
        Assert.Equal(521726468.3071998, et.Epoch.Value);
        Assert.Equal(TimeRepresentationPrecision.EtSeconds, et.RepresentationPrecision);

        var jd = resolver.Resolve(EpochInputKind.JulianDateTdb, "2457593.5000035");
        Assert.True(jd.Success);
        Assert.Equal(TimeRepresentationPrecision.JulianDateDouble, jd.RepresentationPrecision);
        Assert.Contains("tens of microseconds", jd.Provenance);
        Assert.False(resolver.Resolve(EpochInputKind.EtSeconds, "NaN").Success);
        Assert.False(resolver.Resolve(EpochInputKind.EtSeconds, "Infinity").Success);
        Assert.False(resolver.Resolve(EpochInputKind.Utc, "2026-10-18T12:00:00Z").Success);
    }

    [Fact]
    public void EtStepping_AddsToEtDirectly_AndRejectsOverflow()
    {
        var before = new EtSeconds(521726468.3071998);
        var after = EpochInputResolver.StepEt(before, 3600);
        Assert.Equal(before.Value + 3600, after.Value);
        Assert.Throws<ArgumentOutOfRangeException>(() => EpochInputResolver.StepEt(
            new EtSeconds(double.MaxValue), double.MaxValue));
    }

    [Fact]
    public void OutOfCoverageEpoch_IsRejectedWithoutExtrapolation()
    {
        var error = Assert.Throws<SceneBuildException>(() => fixture.Builder.Build(new SceneBuildRequest(
            new EtSeconds(fixture.Provider.Segments.Min(s => s.InitEt) - 1))));
        Assert.Contains("outside", error.Message);
        Assert.Contains("No extrapolation", error.Message);
    }

    [Fact]
    public void SceneCoverage_ClampsToTheIntersectionOfAllRequiredTargets()
    {
        var required = SceneBuilder.Definitions.Where(d => d.Id != "moon").ToArray();
        double start = required.Max(d => fixture.Provider.Segments.Single(s => s.BodyId == d.Id).InitEt);
        double end = required.Min(d => fixture.Provider.Segments.Single(s => s.BodyId == d.Id).EndEt);
        var atStart = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(start)));
        var atEnd = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(end)));
        Assert.Equal(start, atStart.Epoch.Value);
        Assert.Equal(end, atEnd.Epoch.Value);
        Assert.Throws<SceneBuildException>(() => fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(start - 0.01))));
        Assert.Throws<SceneBuildException>(() => fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(end + 0.01))));
    }

    [Fact]
    public void Trails_AreActualChronologicalEphemerisSamples_AndStrictlyBounded()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0), IncludeTrails: true));
        Assert.InRange(scene.TrailSampleCount, 1, SceneBuilder.MaximumTotalTrailSamples);
        foreach (var item in scene.Objects.Where(x => x.Definition?.SamplingPeriodDays is not null))
        {
            Assert.InRange(item.Trail.Count, 1, SceneBuilder.MaximumTrailSamplesPerObject);
            Assert.Equal(item.Trail.OrderBy(x => x.EtSeconds).Select(x => x.EtSeconds),
                item.Trail.Select(x => x.EtSeconds));
            Assert.All(item.Trail, point => Assert.True(double.IsFinite(point.PositionMeters.X)));
        }
        Assert.Empty(scene.FindObject("sun")!.Trail);
    }

    [Fact]
    public void CanceledOrOutdatedSceneTicket_CannotBeAccepted()
    {
        var gate = new SceneGenerationGate();
        long first = gate.BeginRequest();
        long second = gate.BeginRequest();
        Assert.False(gate.IsCurrent(first));
        Assert.True(gate.IsCurrent(second));
        gate.CancelPending();
        Assert.False(gate.IsCurrent(second));
    }

    [Fact]
    public void Camera_ProjectsAndUnprojectsDoublePrecisionWorldCoordinates()
    {
        var camera = new Camera2D();
        camera.SetCenter(0, 0);
        camera.SetScale(2e-10);
        var viewport = new Viewport(11, 17, 1000, 600);
        Assert.True(camera.TryWorldToScreen(1.25e11, -7.5e10, viewport, out var point));
        Assert.True(camera.TryScreenToWorld(point.X, point.Y, viewport, out var x, out var y));
        Assert.InRange(Math.Abs(x - 1.25e11), 0, 1e-3);
        Assert.InRange(Math.Abs(y + 7.5e10), 0, 1e-3);
    }

    [Fact]
    public void Camera_FloatingOriginPreservesSmallSeparationsAtLargeAbsoluteCoordinates()
    {
        var camera = new Camera2D();
        const double basePosition = 1e15;
        camera.SetCenter(basePosition, basePosition);
        camera.SetScale(1);
        var viewport = new Viewport(0, 0, 100, 100);
        Assert.True(camera.TryWorldToScreen(basePosition + 250, basePosition - 125, viewport, out var p));
        Assert.Equal(52.5, p.X);
        Assert.Equal(51.25, p.Y);
    }

    [Fact]
    public void Camera_ZoomAtPointerKeepsTheUnderPointerWorldPointFixed_AndBoundsScale()
    {
        var camera = new Camera2D();
        camera.SetCenter(100, -200);
        camera.SetScale(1e-8);
        var viewport = new Viewport(8, 6, 500, 300);
        double px = 150, py = 72;
        Assert.True(camera.TryScreenToWorld(px, py, viewport, out double beforeX, out double beforeY));
        Assert.True(camera.ZoomAt(px, py, 5, viewport));
        Assert.True(camera.TryScreenToWorld(px, py, viewport, out double afterX, out double afterY));
        Assert.InRange(Math.Abs(beforeX - afterX), 0, 1e-5);
        Assert.InRange(Math.Abs(beforeY - afterY), 0, 1e-5);
        Assert.Equal(5e-8, camera.PixelsPerMeter);
        Assert.True(camera.ZoomAt(px, py, 1e100, viewport));
        Assert.Equal(Camera2D.MaximumPixelsPerMeter, camera.PixelsPerMeter);
        Assert.True(camera.ZoomAt(px, py, 1e-100, viewport));
        Assert.Equal(Camera2D.MinimumPixelsPerMeter, camera.PixelsPerMeter);
    }

    [Fact]
    public void Camera_PanUsesDisplayPixels_AndDoesNotChangeAnyScientificState()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        var earthPosition = scene.FindObject("earth")!.State.Position;
        var camera = new Camera2D();
        camera.SetCenter(0, 0);
        camera.SetScale(2e-8);
        Assert.True(camera.PanPixels(80, -40));
        Assert.Equal(-4e9, camera.CenterX);
        Assert.Equal(-2e9, camera.CenterY);
        Assert.Equal(earthPosition, scene.FindObject("earth")!.State.Position);
    }

    [Fact]
    public void Camera_HitTestsReadableClickableObjectSymbols()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        var camera = new Camera2D();
        var viewport = new Viewport(0, 0, 800, 600);
        camera.Fit(scene.Objects.Select(x => x.State.Position), viewport);
        var earth = scene.FindObject("earth")!;
        Assert.True(camera.TryWorldToScreen(earth.State.Position.X, earth.State.Position.Y, viewport, out var point));
        Assert.Equal("earth", Camera2D.HitTest(scene, camera, point.X, point.Y, viewport, 11)?.Id);
    }

    [Fact]
    public void Camera_FitSolarSystemKeepsEverySceneObjectInsideTheMap()
    {
        var scene = fixture.Builder.Build(new SceneBuildRequest(new EtSeconds(0)));
        var camera = new Camera2D();
        var viewport = new Viewport(20, 30, 1200, 700);
        camera.Fit(scene.Objects.Select(x => x.State.Position), viewport);
        foreach (var item in scene.Objects)
        {
            Assert.True(camera.TryWorldToScreen(item.State.Position.X, item.State.Position.Y, viewport, out var point));
            Assert.InRange(point.X, viewport.X, viewport.X + viewport.Width);
            Assert.InRange(point.Y, viewport.Y, viewport.Y + viewport.Height);
        }
    }

    [Fact]
    public void Camera_RejectsNonFiniteCoordinatesAndFitHandlesHugeFiniteValues()
    {
        var camera = new Camera2D();
        var viewport = new Viewport(0, 0, 640, 480);
        Assert.False(camera.TryWorldToScreen(double.NaN, 0, viewport, out _));
        Assert.False(camera.TryScreenToWorld(double.PositiveInfinity, 0, viewport, out _, out _));
        Assert.False(camera.ZoomAt(0, 0, double.NaN, viewport));
        Assert.Throws<ArgumentOutOfRangeException>(() => camera.SetCenter(double.PositiveInfinity, 0));
        camera.Fit([new Vector3D(-double.MaxValue, 0, 0), new Vector3D(double.MaxValue, 0, 0)], viewport);
        Assert.True(double.IsFinite(camera.CenterX));
        Assert.InRange(camera.PixelsPerMeter, Camera2D.MinimumPixelsPerMeter, Camera2D.MaximumPixelsPerMeter);
    }
}
