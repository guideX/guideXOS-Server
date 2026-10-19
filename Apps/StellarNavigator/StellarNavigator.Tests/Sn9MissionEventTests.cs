using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using StellarNavigator.Scene;

namespace StellarNavigator.Tests;

public sealed class Sn9MissionEventTests
{
    [Fact]
    public void ClosestApproach_RefinesAnalyticLinearRelativeMotionInSiUnits()
    {
        var setup = CreateTrajectory(0, 10, 10, currentEt: 4.8);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup, et =>
            (new Vector3D(3.0 * (et - 5.0), 12.0, 0), new Vector3D(3.0, 0, 0)));

        var timeline = Analyze(setup, ephemeris);
        var closest = Assert.Single(Approaches(timeline));

        Assert.True(closest.IsRefined);
        Assert.InRange(closest.EtSeconds, 5.0 - 1e-5, 5.0 + 1e-5);
        Assert.InRange(closest.DistanceMeters!.Value, 12.0 - 1e-6, 12.0 + 1e-6);
        Assert.InRange(closest.RelativeSpeedMetersPerSecond!.Value, 3.0 - 1e-5, 3.0 + 1e-5);
        Assert.Equal("earth", closest.ObjectId);
        Assert.Contains(timeline.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("sample gap", StringComparison.Ordinal));
    }

    [Theory]
    [InlineData(true, 0.0)]
    [InlineData(false, 10.0)]
    public void ClosestApproach_ReportsSupportedWindowBoundary(bool minimumAtStart, double expectedEpoch)
    {
        var setup = CreateTrajectory(0, 10, 11);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup, et =>
        {
            double x = minimumAtStart ? et : et - 10.0;
            return (new Vector3D(x, 1.0, 0), new Vector3D(1, 0, 0));
        });

        var closest = Assert.Single(Approaches(Analyze(setup, ephemeris)));

        Assert.False(closest.IsRefined);
        Assert.Equal(expectedEpoch, closest.EtSeconds);
        Assert.InRange(closest.DistanceMeters!.Value, 1.0 - 1e-8, 1.0 + 1e-8);
    }

    [Fact]
    public void MultipleLocalMinima_AreFoundInChronologicalOrderAndRefined()
    {
        var setup = CreateTrajectory(0, 10, 100);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup, et =>
        {
            double omega = Math.PI / 5.0;
            return (new Vector3D(Math.Cos(omega * et), 1e-3, 0),
                new Vector3D(-omega * Math.Sin(omega * et), 0, 0));
        });

        var closest = Approaches(Analyze(setup, ephemeris));

        Assert.Equal(2, closest.Length);
        Assert.All(closest, item => Assert.True(item.IsRefined));
        Assert.InRange(closest[0].EtSeconds, 2.5 - 2e-4, 2.5 + 2e-4);
        Assert.InRange(closest[1].EtSeconds, 7.5 - 2e-4, 7.5 + 2e-4);
        Assert.All(closest, item => Assert.InRange(item.DistanceMeters!.Value, 1e-3 - 1e-8, 1e-3 + 1e-8));
    }

    [Fact]
    public void EqualTimeEvents_UseDocumentedStableTypeAndIdTieBreaks()
    {
        var maneuver = new ImpulsiveManeuver("M-start", 0, new Vector3D(0, 0.1, 0));
        var setup = CreateTrajectory(0, 4, 5, [maneuver]);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup,
            et => (new Vector3D(et, 0, 0), new Vector3D(1, 0, 0)), maneuvers: [maneuver]);
        var analyzer = new MissionEventAnalyzer(setup.Propagator, ephemeris);

        var first = analyzer.Analyze(setup.Trajectory, "earth");
        var second = analyzer.Analyze(setup.Trajectory, "earth");
        var atStart = first.Events.Where(item => item.EtSeconds == 0).ToArray();

        Assert.Equal(first.Events, second.Events);
        Assert.Equal(MissionEventType.SimulationStart, atStart[0].Type);
        Assert.Equal(MissionEventType.Maneuver, atStart[1].Type);
        Assert.Equal(MissionEventType.ClosestApproach, atStart[2].Type);
        Assert.True(atStart.Skip(3).All(item => item.Type == MissionEventType.Warning));
        Assert.Contains(first.Events, item => item.Type == MissionEventType.Maneuver && item.ManeuverStatus == "Applied");
    }

    [Fact]
    public void ManeuverBoundary_IsOneSidedAndNeverRefinedAcrossImpulse()
    {
        const double maneuverEt = 5.0;
        var maneuver = new ImpulsiveManeuver("M-boundary", maneuverEt, new Vector3D(0, 500, 0));
        var setup = CreateTrajectory(0, 10, 6, [maneuver]);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup,
            et => (new Vector3D(Math.Abs(et - maneuverEt), 0, 0),
                new Vector3D(et < maneuverEt ? -1 : 1, 0, 0)), maneuvers: [maneuver]);

        var closest = Assert.Single(Approaches(Analyze(setup, ephemeris)));

        Assert.Equal(maneuverEt, closest.EtSeconds);
        Assert.InRange(closest.DistanceMeters!.Value, 0.0, 1e-4);
        Assert.False(closest.IsRefined);
        Assert.Null(closest.RelativeSpeedMetersPerSecond);
        Assert.Null(closest.SegmentIndex);
    }

    [Fact]
    public void DisabledManeuver_IsListedWithoutChangingTrajectoryApplicationStatus()
    {
        var maneuver = new ImpulsiveManeuver("M-disabled", 5, new Vector3D(2, 0, 0), Enabled: false);
        var setup = CreateTrajectory(0, 10, 6, [maneuver]);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup, _ =>
            (new Vector3D(100, 0, 0), new Vector3D(0, 0, 0)), maneuvers: [maneuver]);

        var item = Assert.Single(Analyze(setup, ephemeris).Events,
            item => item.Type == MissionEventType.Maneuver);

        Assert.Equal("Disabled", item.ManeuverStatus);
        Assert.Equal(2.0, item.DeltaVMagnitudeMetersPerSecond);
    }

    [Fact]
    public void EphemerisGapAndInvalidSamples_SplitRunsAndReturnWarnings()
    {
        var setup = CreateTrajectory(0, 10, 11);
        var ephemeris = SyntheticEphemeris.FollowTrajectory(setup, et =>
            (new Vector3D(et - 5, 1, 0), new Vector3D(1, 0, 0)), gap: (4.5, 5.5));
        var gapTimeline = Analyze(setup, ephemeris);
        var invalidSamples = setup.Trajectory.Samples.ToArray();
        invalidSamples[2] = invalidSamples[2] with
        {
            State = invalidSamples[2].State with { EtSeconds = double.NaN },
        };
        var invalidTimeline = Analyze(setup with
        {
            Trajectory = setup.Trajectory with { Samples = invalidSamples },
        }, ephemeris);

        Assert.Contains(gapTimeline.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("unavailable", StringComparison.OrdinalIgnoreCase));
        Assert.DoesNotContain(Approaches(gapTimeline), item => item.EtSeconds > 4.5 && item.EtSeconds < 5.5);
        Assert.Contains(invalidTimeline.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("non-finite", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void UnsupportedTargetAndIncompatibleFrameFailClosed()
    {
        var setup = CreateTrajectory(0, 4, 5);
        var unknown = Analyze(setup, new SyntheticEphemeris(setup, _ =>
            (new Vector3D(0, 0, 0), new Vector3D(0, 0, 0))), "pluto");
        var wrongFrameProvider = SyntheticEphemeris.FollowTrajectory(setup, _ =>
            (new Vector3D(10, 0, 0), new Vector3D(0, 0, 0)), wrongFrame: true);
        var wrongFrame = Analyze(setup, wrongFrameProvider);

        Assert.Empty(Approaches(unknown));
        Assert.Contains(unknown.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("not supported", StringComparison.Ordinal));
        Assert.Empty(Approaches(wrongFrame));
        Assert.Contains(wrongFrame.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("coordinate frame", StringComparison.Ordinal));
    }

    [Fact]
    public void ZeroAndLargeDistances_AreFiniteAndDeterministic()
    {
        var setup = CreateTrajectory(0, 4, 5);
        var zero = Analyze(setup, SyntheticEphemeris.FollowTrajectory(setup, _ =>
            (new Vector3D(0, 0, 0), new Vector3D(0, 0, 0))));
        var large = Analyze(setup, new SyntheticEphemeris(setup, _ =>
            (new Vector3D(1e150, -1e150, 1e150), new Vector3D(0, 0, 0))));
        var zeroApproach = Assert.Single(Approaches(zero), item => item.EtSeconds == 0);
        var largeApproach = Assert.Single(Approaches(large), item => item.EtSeconds == 0);

        Assert.Equal(0.0, zeroApproach.DistanceMeters!.Value);
        Assert.True(double.IsFinite(largeApproach.DistanceMeters!.Value));
        Assert.InRange(largeApproach.DistanceMeters!.Value / 1e150, 1.73, 1.74);
    }

    [Fact]
    public void LongIntervalAndNonFiniteEphemeris_AreBoundedAndFailClosed()
    {
        var longSetup = CreateTrajectory(0, 100_000_000, 20,
            currentEt: 43_000_000, initialRadiusMeters: 1e15);
        var longTimeline = Analyze(longSetup, new SyntheticEphemeris(longSetup,
            _ => (new Vector3D(0, 0, 0), new Vector3D(0, 0, 0))), "sun");
        var longApproaches = Approaches(longTimeline);

        var shortSetup = CreateTrajectory(0, 4, 5);
        var invalidEphemeris = SyntheticEphemeris.FollowTrajectory(shortSetup, _ =>
            (new Vector3D(double.NaN, 0, 0), new Vector3D(0, 0, 0)));
        var invalid = Analyze(shortSetup, invalidEphemeris);
        var insufficientSetup = CreateTrajectory(0, 0, 2);
        var insufficient = Analyze(insufficientSetup,
            SyntheticEphemeris.FollowTrajectory(insufficientSetup, _ =>
                (new Vector3D(1, 0, 0), new Vector3D(0, 0, 0))));

        Assert.NotEmpty(longApproaches);
        Assert.True(longTimeline.MaximumSampleIntervalSeconds > 5_000_000);
        Assert.All(longApproaches, item =>
        {
            Assert.InRange(item.EtSeconds, longTimeline.StartEtSeconds, longTimeline.EndEtSeconds);
            Assert.True(double.IsFinite(item.DistanceMeters!.Value));
        });
        Assert.True(longTimeline.Events.Count <= MissionEventAnalyzer.MaximumEvents);
        Assert.Empty(Approaches(invalid));
        Assert.Contains(invalid.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("non-finite SI", StringComparison.Ordinal));
        Assert.Empty(Approaches(insufficient));
        Assert.Contains(insufficient.Events, item => item.Type == MissionEventType.Warning &&
            item.Warning!.Contains("two trajectory samples", StringComparison.Ordinal));
    }

    [Fact]
    public void RecomputedManeuverScenario_ReplacesBaselineApproachResults()
    {
        var baseline = CreateTrajectory(0, 20, 21);
        var maneuver = new ImpulsiveManeuver("M-change", 10, new Vector3D(0, 700, 0));
        var changed = CreateTrajectory(0, 20, 21, [maneuver]);
        var target = SyntheticEphemeris.FollowTrajectory(baseline, et =>
            (new Vector3D(0, et - 15, 0), new Vector3D(0, 1, 0)));

        var oldEvents = Analyze(baseline, target).Events;
        var newEvents = Analyze(changed, target).Events;

        Assert.NotEqual(oldEvents, newEvents);
        Assert.Contains(newEvents, item => item.Type == MissionEventType.Maneuver && item.ManeuverStatus == "Applied");
        Assert.NotEqual(Approaches(oldEvents).Select(item => item.EtSeconds),
            Approaches(newEvents).Select(item => item.EtSeconds));
    }

    private static MissionEventTimeline Analyze(ScenarioSetup setup, SyntheticEphemeris ephemeris,
        string targetBodyId = "earth") => new MissionEventAnalyzer(setup.Propagator, ephemeris)
        .Analyze(setup.Trajectory, targetBodyId);

    private static MissionEvent[] Approaches(MissionEventTimeline timeline) =>
        timeline.Events.Where(item => item.Type == MissionEventType.ClosestApproach).ToArray();

    private static MissionEvent[] Approaches(IReadOnlyList<MissionEvent> events) =>
        events.Where(item => item.Type == MissionEventType.ClosestApproach).ToArray();

    private static ScenarioSetup CreateTrajectory(double startEt, double endEt, int count,
        IReadOnlyList<ImpulsiveManeuver>? maneuvers = null, double? currentEt = null,
        double initialRadiusMeters = 10_000_000_000.0)
    {
        string gmPath = Path.Combine(AppContext.BaseDirectory, "Data", SunGravitationalParameter.RuntimeAssetName);
        var gm = SunGravitationalParameter.Load(gmPath);
        var propagator = new TwoBodyPropagator(gm);
        var scenario = SimulatedTrajectoryScenario.CreateDemonstration(gm) with
        {
            InitialState = new OrbitState(0, new Vector3D(initialRadiusMeters, 0, 0),
                new Vector3D(0, Math.Sqrt(gm.MuMeters3PerSecond2 / initialRadiusMeters), 0)),
        };
        var generated = new TrajectoryGenerator(propagator).Generate(scenario,
            maneuvers ?? Array.Empty<ImpulsiveManeuver>(),
            new TrajectorySamplingRequest(currentEt ?? (startEt + endEt) * 0.5, startEt, endEt, count));
        Assert.True(generated.IsSuccess, generated.Error);
        return new ScenarioSetup(scenario, propagator, generated.Trajectory!);
    }

    private sealed record ScenarioSetup(SimulatedTrajectoryScenario Scenario,
        TwoBodyPropagator Propagator, SimulatedTrajectory Trajectory);

    private sealed class SyntheticEphemeris : IEphemerisProvider, IEtEphemerisProvider
    {
        private readonly ScenarioSetup _setup;
        private readonly Func<double, (Vector3D Position, Vector3D Velocity)> _relativeMotion;
        private readonly IReadOnlyList<ImpulsiveManeuver> _maneuvers;
        private readonly (double Start, double End)? _gap;
        private readonly bool _wrongFrame;

        public SyntheticEphemeris(ScenarioSetup setup,
            Func<double, (Vector3D Position, Vector3D Velocity)> relativeMotion,
            IReadOnlyList<ImpulsiveManeuver>? maneuvers = null,
            (double Start, double End)? gap = null,
            bool wrongFrame = false)
        {
            _setup = setup;
            _relativeMotion = relativeMotion;
            _maneuvers = maneuvers ?? Array.Empty<ImpulsiveManeuver>();
            _gap = gap;
            _wrongFrame = wrongFrame;
        }

        internal static SyntheticEphemeris FollowTrajectory(ScenarioSetup setup,
            Func<double, (Vector3D Position, Vector3D Velocity)> relativeMotion,
            IReadOnlyList<ImpulsiveManeuver>? maneuvers = null,
            (double Start, double End)? gap = null,
            bool wrongFrame = false) => new(setup, relativeMotion, maneuvers, gap, wrongFrame);

        public string DataSource => "SN9 deterministic synthetic fixture";
        public ReferenceFrame NativeFrame => ReferenceFrame.Barycentric;

        public bool IsBodySupported(string bodyId) => bodyId is "sun" or "earth";

        public (AstroTime Start, AstroTime End)? GetCoverage(string bodyId) => !IsBodySupported(bodyId)
            ? null
            : (ToTime(-TwoBodyPropagator.MaximumPropagationHorizonSeconds),
                ToTime(TwoBodyPropagator.MaximumPropagationHorizonSeconds));

        public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
            string bodyId, AstroTime epoch, ReferenceFrame targetFrame) =>
            GetStateAtEt(bodyId, new EtSeconds((epoch.JdUtc - 2_451_545.0) * 86_400.0), targetFrame);

        public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetStateAtEt(
            string bodyId, EtSeconds epoch, ReferenceFrame targetFrame)
        {
            if (!IsBodySupported(bodyId))
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InsufficientInformation, "Unsupported synthetic body.");
            if (_gap is { } gap && bodyId == "earth" && epoch.Value > gap.Start && epoch.Value < gap.End)
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.StaleData, "Synthetic ephemeris gap.");

            Vector3D position;
            Vector3D velocity;
            if (bodyId == "sun")
            {
                position = new Vector3D(0, 0, 0);
                velocity = new Vector3D(0, 0, 0);
            }
            else
            {
                OrbitState spacecraft = StateAt(epoch.Value);
                var relative = _relativeMotion(epoch.Value);
                position = spacecraft.PositionMeters - relative.Position;
                velocity = spacecraft.VelocityMetersPerSecond - relative.Velocity;
            }

            var frame = _wrongFrame ? ReferenceFrame.IcrfJ2000 : ReferenceFrame.Barycentric;
            return NavigationResult<(Vector3D, Vector3D)>.Success((position, velocity),
                NavigationResultStatus.ValidatedReference,
                ToTime(epoch.Value), frame, DataSource);
        }

        private OrbitState StateAt(double et)
        {
            OrbitState state = _setup.Scenario.InitialState;
            foreach (var maneuver in _maneuvers.OrderBy(item => item.EtSeconds))
            {
                if (maneuver.EtSeconds > et)
                    break;
                var pre = _setup.Propagator.Propagate(state, maneuver.EtSeconds);
                if (!pre.IsSuccess)
                    throw new InvalidOperationException(pre.Error);
                state = pre.State!.Value;
                if (maneuver.Enabled)
                    state = state with { VelocityMetersPerSecond = state.VelocityMetersPerSecond + maneuver.DeltaVelocityMetersPerSecond };
            }
            var result = _setup.Propagator.Propagate(state, et);
            if (!result.IsSuccess)
                throw new InvalidOperationException(result.Error);
            return result.State!.Value;
        }

        private static AstroTime ToTime(double et) =>
            new(new EtSeconds(et).ToJulianDateTdb(), TimeScale.Tdb);
    }
}
