using System.Text.Json;
using Astronomy.Core;
using Navigation.Core;

namespace StellarNavigator.Tests;

public sealed class Sn8TrajectoryTests
{
    private const double PositionRelativeTolerance = 2.0e-13;
    private const double PositionAbsoluteFloorMeters = 0.01;
    private const double VelocityRelativeTolerance = 2.0e-13;
    private const double VelocityAbsoluteFloorMetersPerSecond = 1.0e-7;

    [Fact]
    public void SolarGmAsset_IsAuthenticatedAndRejectsMissingOrCorruptData()
    {
        SunGravitationalParameter gm = LoadGm();
        Assert.Equal(SunGravitationalParameter.RuntimeAssetIdentity, gm.Identity);
        Assert.Equal(1.3271244004127942e20, gm.MuMeters3PerSecond2);
        Assert.Contains(SunGravitationalParameter.SourceSha256, gm.Provenance);

        string source = GmAssetPath();
        string corrupted = Path.Combine(Path.GetTempPath(), $"sn8-gm-corrupt-{Guid.NewGuid():N}.json");
        try
        {
            File.Copy(source, corrupted);
            File.AppendAllText(corrupted, " ");
            Assert.Throws<InvalidDataException>(() => SunGravitationalParameter.Load(corrupted));
            Assert.Throws<FileNotFoundException>(() => SunGravitationalParameter.Load(corrupted + ".missing"));
        }
        finally
        {
            if (File.Exists(corrupted))
                File.Delete(corrupted);
        }
    }

    [Fact]
    public void UniversalVariables_MatchTwoHundredIndependentCspiceProp2bStates()
    {
        var document = JsonSerializer.Deserialize<ReferenceDocument>(
            File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "sn8_propagation_reference.json")),
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true })!;
        Assert.Equal("CSPICE_N0067", document.Metadata.Toolkit);
        Assert.Equal("prop2b_c", document.Metadata.Routine);
        Assert.Equal(200, document.Metadata.ComparisonCount);
        Assert.Equal(200, document.Cases.Length);
        Assert.Equal(SunGravitationalParameter.SourceSha256, document.Metadata.GmSourceSha256);

        var propagator = new TwoBodyPropagator(LoadGm());
        var positionErrors = new List<double>(document.Cases.Length);
        var positionRelativeErrors = new List<double>(document.Cases.Length);
        var velocityErrors = new List<double>(document.Cases.Length);
        var velocityRelativeErrors = new List<double>(document.Cases.Length);
        var comparisonFailures = new List<string>();
        var worstPosition = (Id: "", Error: 0.0);
        var worstVelocity = (Id: "", Error: 0.0);
        foreach (var testCase in document.Cases)
        {
            var initial = new OrbitState(testCase.InitialEtSeconds,
                Vector(testCase.InitialPositionMeters), Vector(testCase.InitialVelocityMetersPerSecond));
            var result = propagator.Propagate(initial, testCase.InitialEtSeconds + testCase.DtSeconds);
            Assert.True(result.IsSuccess,
                $"{testCase.Id} ({testCase.Regime}) failed after {result.Iterations} iterations: {result.Error}");
            Assert.True(result.Iterations <= TwoBodyPropagator.MaximumSolverIterations);
            var state = result.State!.Value;
            double pAbs = (state.PositionMeters - Vector(testCase.ExpectedPositionMeters)).Magnitude;
            double vAbs = (state.VelocityMetersPerSecond - Vector(testCase.ExpectedVelocityMetersPerSecond)).Magnitude;
            double pScale = Vector(testCase.ExpectedPositionMeters).Magnitude;
            double vScale = Vector(testCase.ExpectedVelocityMetersPerSecond).Magnitude;
            double pRel = pAbs / pScale;
            double vRel = vAbs / vScale;
            positionErrors.Add(pAbs);
            positionRelativeErrors.Add(pRel);
            velocityErrors.Add(vAbs);
            velocityRelativeErrors.Add(vRel);
            if (pAbs > worstPosition.Error) worstPosition = (testCase.Id, pAbs);
            if (vAbs > worstVelocity.Error) worstVelocity = (testCase.Id, vAbs);

            if (pAbs > PositionAbsoluteFloorMeters + pScale * PositionRelativeTolerance ||
                pRel > PositionRelativeTolerance)
                comparisonFailures.Add($"{testCase.Id}/{testCase.Regime}: position {pAbs:R} m, {pRel:R} relative, dt={testCase.DtSeconds:R}s");
            if (vAbs > VelocityAbsoluteFloorMetersPerSecond + vScale * VelocityRelativeTolerance ||
                vRel > VelocityRelativeTolerance)
                comparisonFailures.Add($"{testCase.Id}/{testCase.Regime}: velocity {vAbs:R} m/s, {vRel:R} relative, dt={testCase.DtSeconds:R}s");
        }

        double pRms = Rms(positionErrors);
        double prRms = Rms(positionRelativeErrors);
        double vRms = Rms(velocityErrors);
        double vrRms = Rms(velocityRelativeErrors);
        Console.WriteLine($"CSPICE N0067 prop2b_c comparisons={document.Cases.Length}; " +
            $"position max={positionErrors.Max():R} m ({positionRelativeErrors.Max():R} relative), " +
            $"RMS={pRms:R} m ({prRms:R} relative), worst={worstPosition.Id}; " +
            $"velocity max={velocityErrors.Max():R} m/s ({velocityRelativeErrors.Max():R} relative), " +
            $"RMS={vRms:R} m/s ({vrRms:R} relative), worst={worstVelocity.Id}; " +
            $"tolerances=position {PositionAbsoluteFloorMeters:R} m + {PositionRelativeTolerance:R} relative, " +
            $"velocity {VelocityAbsoluteFloorMetersPerSecond:R} m/s + {VelocityRelativeTolerance:R} relative");
        Assert.True(comparisonFailures.Count == 0,
            $"{comparisonFailures.Count} of {document.Cases.Length} CSPICE comparisons exceeded tolerance. " +
            $"Position max/RMS={positionErrors.Max():R}/{pRms:R} m; velocity max/RMS={velocityErrors.Max():R}/{vRms:R} m/s. " +
            string.Join(" | ", comparisonFailures.Take(8)));
    }

    [Fact]
    public void ZeroInterval_ReturnsExactInitialState_AndRepeatedEvaluationIsDeterministic()
    {
        var gm = LoadGm();
        var propagator = new TwoBodyPropagator(gm);
        var state = new OrbitState(12345.25, new Vector3D(2.0e11, -1.0e10, 3.0e9),
            new Vector3D(1000.0, 28000.0, -50.0));
        var identity = propagator.Propagate(state, state.EtSeconds);
        Assert.True(identity.IsSuccess);
        Assert.Equal(state, identity.State);
        var first = propagator.Propagate(state, state.EtSeconds + 86400.0);
        var second = propagator.Propagate(state, state.EtSeconds + 86400.0);
        Assert.Equal(first, second);
    }

    [Fact]
    public void CircularOrbit_HalfPeriodAndFullPeriodBehaveAsExpected()
    {
        var gm = LoadGm();
        var propagator = new TwoBodyPropagator(gm);
        const double radius = 1.0e11;
        double speed = Math.Sqrt(gm.MuMeters3PerSecond2 / radius);
        double period = 2 * Math.PI * Math.Sqrt(radius * radius * radius / gm.MuMeters3PerSecond2);
        var initial = new OrbitState(0, new Vector3D(radius, 0, 0), new Vector3D(0, speed, 0));
        var half = propagator.Propagate(initial, period * 0.5);
        var full = propagator.Propagate(initial, period);
        Assert.True(half.IsSuccess, half.Error);
        Assert.True(full.IsSuccess, full.Error);
        Assert.InRange((half.State!.Value.PositionMeters + initial.PositionMeters).Magnitude, 0, 0.02);
        Assert.InRange((half.State.Value.VelocityMetersPerSecond + initial.VelocityMetersPerSecond).Magnitude, 0, 2e-8);
        Assert.InRange((full.State!.Value.PositionMeters - initial.PositionMeters).Magnitude, 0, 0.02);
        Assert.InRange((full.State.Value.VelocityMetersPerSecond - initial.VelocityMetersPerSecond).Magnitude, 0, 2e-8);
    }

    [Fact]
    public void CoastPreservesSpecificEnergyAndAngularMomentum_AndReversePropagationReturnsNearInitial()
    {
        var gm = LoadGm();
        var propagator = new TwoBodyPropagator(gm);
        var initial = SimulatedTrajectoryScenario.CreateDemonstration(gm).InitialState;
        double energy0 = SpecificEnergy(initial, gm.MuMeters3PerSecond2);
        double h0 = Cross(initial.PositionMeters, initial.VelocityMetersPerSecond).Magnitude;
        var forward = propagator.Propagate(initial, 3.0 * TwoBodyPropagator.SecondsPerJulianYear);
        Assert.True(forward.IsSuccess, forward.Error);
        var final = forward.State!.Value;
        double energyScale = gm.MuMeters3PerSecond2 / initial.PositionMeters.Magnitude;
        Assert.InRange(Math.Abs(SpecificEnergy(final, gm.MuMeters3PerSecond2) - energy0) / energyScale, 0, 2e-13);
        double h1 = Cross(final.PositionMeters, final.VelocityMetersPerSecond).Magnitude;
        Assert.InRange(Math.Abs(h1 - h0) / h0, 0, 2e-13);
        var reversed = propagator.Propagate(final, initial.EtSeconds);
        Assert.True(reversed.IsSuccess, reversed.Error);
        Assert.InRange((reversed.State!.Value.PositionMeters - initial.PositionMeters).Magnitude, 0, 0.05);
        Assert.InRange((reversed.State.Value.VelocityMetersPerSecond - initial.VelocityMetersPerSecond).Magnitude, 0, 5e-8);
    }

    [Fact]
    public void SolverReturnsTypedFailuresForInvalidDegenerateAndOutOfHorizonInputs()
    {
        var valid = SimulatedTrajectoryScenario.CreateDemonstration(LoadGm()).InitialState;
        Assert.Equal(TwoBodyPropagationStatus.InvalidInput,
            new TwoBodyPropagator(0).Propagate(valid, 1).Status);
        Assert.Equal(TwoBodyPropagationStatus.InvalidInput,
            new TwoBodyPropagator(double.NaN).Propagate(valid, 1).Status);
        Assert.Equal(TwoBodyPropagationStatus.InvalidInput,
            new TwoBodyPropagator(LoadGm()).Propagate(valid, double.PositiveInfinity).Status);
        Assert.Equal(TwoBodyPropagationStatus.UnsupportedRegime,
            new TwoBodyPropagator(LoadGm()).Propagate(new OrbitState(0,
                new Vector3D(0, 0, 0), new Vector3D(0, 0, 0)), 1).Status);
        Assert.Equal(TwoBodyPropagationStatus.UnsupportedRegime,
            new TwoBodyPropagator(LoadGm()).Propagate(new OrbitState(0,
                new Vector3D(1.0e11, 0, 0), new Vector3D(1000, 0, 0)), 1).Status);
        Assert.Equal(TwoBodyPropagationStatus.HorizonExceeded,
            new TwoBodyPropagator(LoadGm()).Propagate(valid,
                TwoBodyPropagator.MaximumPropagationHorizonSeconds + 1).Status);
        var impossible = new TwoBodyPropagator(LoadGm()).Propagate(
            new OrbitState(0, new Vector3D(1.0e12, 0, 0), new Vector3D(0, 1.0e6, 0)),
            TwoBodyPropagator.MaximumPropagationHorizonSeconds);
        Assert.NotEqual(TwoBodyPropagationStatus.Success, impossible.Status);
        Assert.Null(impossible.State);

        var gm = LoadGm();
        var oneIteration = new TwoBodyPropagator(gm.MuMeters3PerSecond2,
            "one-iteration convergence boundary", maximumIterations: 1)
            .Propagate(valid, 86400.0);
        Assert.Equal(TwoBodyPropagationStatus.NonConvergent, oneIteration.Status);
        Assert.Null(oneIteration.State);
    }

    [Fact]
    public void ScenarioValidationRejectsNonFiniteAndUnsupportedCentralBodyMetadata()
    {
        var scenario = SimulatedTrajectoryScenario.CreateDemonstration(LoadGm());
        Assert.Null(scenario.Validate());
        Assert.Contains("Sun-relative", (scenario with { CentralBodyId = "earth" }).Validate());
        Assert.Contains("finite", (scenario with
        {
            InitialState = scenario.InitialState with { EtSeconds = double.NaN },
        }).Validate());
    }

    [Fact]
    public void ManeuverAtExactSampleEpoch_PreservesPositionAndAppliesRequestedDeltaV()
    {
        var generator = NewGenerator(out var scenario);
        const double epoch = 86_400.0;
        var dv = new Vector3D(12.0, -3.0, 0.25);
        var maneuver = new ImpulsiveManeuver("M01", epoch, dv, Label: "test impulse");
        var result = generator.Generate(scenario, [maneuver],
            new TrajectorySamplingRequest(epoch, epoch, epoch, RequestedSampleCount: 2));
        Assert.True(result.IsSuccess, result.Error);
        var trajectory = result.Trajectory!;
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, trajectory.Quality);
        Assert.Equal(epoch, trajectory.CurrentState.EtSeconds);
        Assert.Equal(dv, trajectory.CurrentState.VelocityMetersPerSecond -
            trajectory.Maneuvers[0].PreManeuverState.VelocityMetersPerSecond);
        Assert.Equal(trajectory.Maneuvers[0].PreManeuverState.PositionMeters,
            trajectory.Maneuvers[0].PostManeuverState.PositionMeters);
        Assert.Equal(dv.Magnitude, trajectory.Maneuvers[0].MagnitudeMetersPerSecond, 12);
        Assert.Contains(trajectory.Samples, s => s.Kind == TrajectorySampleKind.ManeuverPre);
        Assert.Contains(trajectory.Samples, s => s.Kind == TrajectorySampleKind.ManeuverPost);
    }

    [Fact]
    public void ManeuverTimeline_IsSortedBoundedAndRejectsDuplicateOrPreInitialEvents()
    {
        var generator = NewGenerator(out var scenario);
        var late = new ImpulsiveManeuver("late", 2000, new Vector3D(0, 1, 0));
        var early = new ImpulsiveManeuver("early", 1000, new Vector3D(0, 2, 0));
        var ordered = generator.Generate(scenario, [late, early],
            new TrajectorySamplingRequest(2000, 0, 2000, 4));
        Assert.True(ordered.IsSuccess, ordered.Error);
        Assert.Equal(new[] { "early", "late" }, ordered.Trajectory!.Maneuvers.Select(m => m.Maneuver.Id));

        var duplicate = generator.Generate(scenario,
            [early, early with { Id = "also-early" }], new TrajectorySamplingRequest(2000, 0, 2000, 4));
        Assert.False(duplicate.IsSuccess);
        Assert.Contains("duplicate ET", duplicate.Error);
        var beforeInitial = generator.Generate(scenario,
            [new ImpulsiveManeuver("before", -1, new Vector3D(1, 0, 0))],
            new TrajectorySamplingRequest(0, -1, 1, 2));
        Assert.False(beforeInitial.IsSuccess);
        Assert.Contains("on or after", beforeInitial.Error);
        Assert.Equal(TwoBodyPropagationStatus.InvalidInput,
            generator.Generate(scenario,
                Enumerable.Range(0, TrajectoryGenerator.MaximumManeuvers + 1)
                    .Select(i => new ImpulsiveManeuver($"M{i}", i + 1, new Vector3D(1, 0, 0))).ToArray(),
                new TrajectorySamplingRequest(1, 0, 2, 2)).Status);
    }

    [Fact]
    public void DisabledManeuverRetainsDeltaVButDoesNotChangeCoastState()
    {
        var generator = NewGenerator(out var scenario);
        var maneuver = new ImpulsiveManeuver("M-disabled", 1234,
            new Vector3D(5, -2, 0), Enabled: false);
        var result = generator.Generate(scenario, [maneuver],
            new TrajectorySamplingRequest(2000, 0, 3000, 5));
        Assert.True(result.IsSuccess, result.Error);
        var execution = Assert.Single(result.Trajectory!.Maneuvers);
        Assert.Equal(maneuver.DeltaVelocityMetersPerSecond.Magnitude, execution.MagnitudeMetersPerSecond, 12);
        Assert.Equal(execution.PreManeuverState.VelocityMetersPerSecond,
            execution.PostManeuverState.VelocityMetersPerSecond);
        Assert.Equal(execution.SegmentBefore, execution.SegmentAfter);
    }

    [Fact]
    public void SamplingIsDeterministicBoundedAndKeepsSimulationQualityConservative()
    {
        var generator = NewGenerator(out var scenario);
        var maneuver = new ImpulsiveManeuver("M1", 1000, new Vector3D(1, 0, 0));
        var request = new TrajectorySamplingRequest(2000, -1000, 3000, 20);
        var first = generator.Generate(scenario, [maneuver], request);
        var second = generator.Generate(scenario, [maneuver], request);
        Assert.True(first.IsSuccess, first.Error);
        Assert.True(second.IsSuccess, second.Error);
        Assert.Equal(first.Trajectory!.Samples, second.Trajectory!.Samples);
        Assert.True(first.Trajectory.Samples.Count <= TrajectoryGenerator.DefaultMaximumSamples);
        Assert.All(first.Trajectory.Samples,
            sample => Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, sample.Quality));
        var provisional = generator.Generate(scenario, [maneuver], request with { ProvisionalTime = true });
        Assert.True(provisional.IsSuccess, provisional.Error);
        Assert.Equal(NavigationResultStatus.StaleData, provisional.Trajectory!.Quality);
        Assert.All(provisional.Trajectory.Samples,
            sample => Assert.Equal(NavigationResultStatus.StaleData, sample.Quality));

        var tooMany = generator.Generate(scenario, [], request with { RequestedSampleCount = 513 });
        Assert.Equal(TwoBodyPropagationStatus.InvalidInput, tooMany.Status);
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Assert.Throws<OperationCanceledException>(() => generator.Generate(scenario, [], request,
            cancelled.Token));
    }

    [Fact]
    public void ScenarioHorizonAndNearBoundaryManeuverAreExplicitlyBounded()
    {
        var generator = NewGenerator(out var scenario);
        double boundary = scenario.InitialState.EtSeconds + scenario.MaximumHorizonSeconds;
        var atBoundary = generator.Generate(scenario,
            [new ImpulsiveManeuver("horizon", boundary, new Vector3D(0.01, 0, 0))],
            new TrajectorySamplingRequest(0, 0, 0, 2));
        Assert.True(atBoundary.IsSuccess, atBoundary.Error);
        var outside = generator.Generate(scenario, [],
            new TrajectorySamplingRequest(boundary + 1, boundary, boundary + 2, 2));
        Assert.Equal(TwoBodyPropagationStatus.HorizonExceeded, outside.Status);
    }

    private static TrajectoryGenerator NewGenerator(out SimulatedTrajectoryScenario scenario)
    {
        SunGravitationalParameter gm = LoadGm();
        scenario = SimulatedTrajectoryScenario.CreateDemonstration(gm);
        return new TrajectoryGenerator(new TwoBodyPropagator(gm));
    }

    private static SunGravitationalParameter LoadGm() => SunGravitationalParameter.Load(GmAssetPath());

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

    private static Vector3D Vector(double[] values) => new(values[0], values[1], values[2]);
    private static double Rms(IReadOnlyList<double> errors) =>
        Math.Sqrt(errors.Sum(value => value * value) / errors.Count);
    private static double SpecificEnergy(OrbitState state, double mu) =>
        (state.VelocityMetersPerSecond.X * state.VelocityMetersPerSecond.X +
         state.VelocityMetersPerSecond.Y * state.VelocityMetersPerSecond.Y +
         state.VelocityMetersPerSecond.Z * state.VelocityMetersPerSecond.Z) * 0.5 -
        mu / state.PositionMeters.Magnitude;
    private static Vector3D Cross(Vector3D a, Vector3D b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X);

    private sealed class ReferenceDocument
    {
        public required ReferenceMetadata Metadata { get; init; }
        public required ReferenceCase[] Cases { get; init; }
    }

    private sealed class ReferenceMetadata
    {
        public required string Toolkit { get; init; }
        public required string Routine { get; init; }
        public int ComparisonCount { get; init; }
        public required string GmSourceSha256 { get; init; }
    }

    private sealed class ReferenceCase
    {
        public required string Id { get; init; }
        public required string Regime { get; init; }
        public double InitialEtSeconds { get; init; }
        public required double[] InitialPositionMeters { get; init; }
        public required double[] InitialVelocityMetersPerSecond { get; init; }
        public double DtSeconds { get; init; }
        public required double[] ExpectedPositionMeters { get; init; }
        public required double[] ExpectedVelocityMetersPerSecond { get; init; }
    }
}
