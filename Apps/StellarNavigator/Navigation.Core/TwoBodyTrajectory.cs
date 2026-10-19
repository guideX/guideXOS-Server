using System.Diagnostics;
using Astronomy.Core;

namespace Navigation.Core;

public enum TwoBodyPropagationStatus
{
    Success,
    InvalidInput,
    UnsupportedRegime,
    HorizonExceeded,
    NonConvergent,
    NumericalFailure,
}

public enum ConicRegime
{
    Elliptical,
    NearParabolic,
    Hyperbolic,
}

public readonly record struct OrbitState(
    double EtSeconds,
    Vector3D PositionMeters,
    Vector3D VelocityMetersPerSecond);

public sealed record TwoBodyPropagationResult(
    TwoBodyPropagationStatus Status,
    OrbitState? State,
    ConicRegime? Regime,
    int Iterations,
    string? Error)
{
    public bool IsSuccess => Status == TwoBodyPropagationStatus.Success && State.HasValue;

    public static TwoBodyPropagationResult Failure(TwoBodyPropagationStatus status, string error, int iterations = 0) =>
        new(status, null, null, iterations, error);
}

/// <summary>
/// Bounded universal-variable two-body propagator using binary64 arithmetic.
/// Units are SI and epochs are ET/TDB seconds past J2000.
/// </summary>
public sealed class TwoBodyPropagator
{
    public const double SecondsPerJulianYear = 365.25 * 86_400.0;
    public const double MaximumPropagationHorizonSeconds = 100.0 * SecondsPerJulianYear;
    public const int MaximumSolverIterations = 100;
    public const double MaximumInitialRadiusMeters = 1.0e16;
    public const double MinimumInitialRadiusMeters = 1.0;
    public const double MaximumInitialSpeedMetersPerSecond = 1.0e6;
    private const double RelativeResidualTolerance = 5.0e-15;
    private const double SmallStumpffArgument = 1.0e-5;
    private const double MaximumHyperbolicArgument = 300.0;
    private const double MaximumEllipticArgument = 1.0e8;

    private readonly double _mu;
    private readonly int _iterationLimit;

    public TwoBodyPropagator(SunGravitationalParameter parameter)
    {
        ArgumentNullException.ThrowIfNull(parameter);
        _mu = parameter.MuMeters3PerSecond2;
        GravitationalParameterIdentity = parameter.Identity;
        Provenance = parameter.Provenance;
        ModelIdentity = $"Newtonian Sun-centered two-body; {parameter.Identity}";
        _iterationLimit = MaximumSolverIterations;
    }

    /// <summary>Direct-parameter constructor for host verification and bounded convergence qualification.</summary>
    public TwoBodyPropagator(double muMeters3PerSecond2, string provenance = "Explicit caller-supplied μ",
        int maximumIterations = MaximumSolverIterations)
    {
        _mu = muMeters3PerSecond2;
        GravitationalParameterIdentity = null;
        Provenance = provenance;
        ModelIdentity = "Newtonian two-body; explicit μ";
        if (maximumIterations is < 1 or > MaximumSolverIterations)
            throw new ArgumentOutOfRangeException(nameof(maximumIterations),
                $"Solver iteration limit must be within 1..{MaximumSolverIterations}.");
        _iterationLimit = maximumIterations;
    }

    public double MuMeters3PerSecond2 => _mu;
    public int SolverIterationLimit => _iterationLimit;
    public string? GravitationalParameterIdentity { get; }
    public string Provenance { get; }
    public string ModelIdentity { get; }

    public TwoBodyPropagationResult Propagate(OrbitState initial, double targetEtSeconds)
    {
        if (!double.IsFinite(_mu) || _mu <= 0)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                "The gravitational parameter μ must be finite and positive.");
        if (!IsFinite(initial) || !double.IsFinite(targetEtSeconds))
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                "Initial state and target ET must contain only finite values.");

        double r0 = Norm(initial.PositionMeters);
        double v0 = Norm(initial.VelocityMetersPerSecond);
        if (!double.IsFinite(r0) || r0 < MinimumInitialRadiusMeters || r0 > MaximumInitialRadiusMeters ||
            !double.IsFinite(v0) || v0 > MaximumInitialSpeedMetersPerSecond)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.UnsupportedRegime,
                $"Initial radius must be in [{MinimumInitialRadiusMeters:R}, {MaximumInitialRadiusMeters:R}] m and speed at most {MaximumInitialSpeedMetersPerSecond:R} m/s.");
        if (v0 == 0)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.UnsupportedRegime,
                "A zero initial velocity is unsupported by this conic propagator.");

        Vector3D h0 = Cross(initial.PositionMeters, initial.VelocityMetersPerSecond);
        double hMagnitude = Norm(h0);
        if (!double.IsFinite(hMagnitude) || hMagnitude == 0)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.UnsupportedRegime,
                "Radial or degenerate initial motion has zero angular momentum and is unsupported.");

        double dt = targetEtSeconds - initial.EtSeconds;
        if (!double.IsFinite(dt))
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                "The requested ET interval is not finite.");
        if (Math.Abs(dt) > MaximumPropagationHorizonSeconds)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.HorizonExceeded,
                $"Propagation interval exceeds the {MaximumPropagationHorizonSeconds:R} second solver horizon.");
        if (dt == 0)
            return new TwoBodyPropagationResult(TwoBodyPropagationStatus.Success, initial,
                Classify(initial.PositionMeters, initial.VelocityMetersPerSecond, _mu), 0, null);

        double speedSquared = Dot(initial.VelocityMetersPerSecond, initial.VelocityMetersPerSecond);
        double alpha = 2.0 / r0 - speedSquared / _mu;
        if (!double.IsFinite(alpha))
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.UnsupportedRegime,
                "Initial energy is outside the finite universal-variable regime.");

        double sqrtMu = Math.Sqrt(_mu);
        double direction = Math.CopySign(1.0, dt);
        double radialDot = Dot(initial.PositionMeters, initial.VelocityMetersPerSecond);
        double a = radialDot / sqrtMu;
        double b = 1.0 - alpha * r0;
        double estimate = sqrtMu * Math.Abs(dt) / r0;
        double upper = Math.Max(estimate, Math.Sqrt(r0) * 1.0e-8);
        bool bracketed = false;
        // Bracketing has its own hard bound; the configurable Newton/bisection
        // iteration limit below controls convergence refinement.
        for (int i = 0; i < MaximumSolverIterations; i++)
        {
            if (!TryKepler(direction * upper, a, b, alpha, r0, sqrtMu * dt,
                    out double f, out _, out _))
                return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                    "Universal Kepler equation overflowed while bracketing the solution.");
            double bracketValue = direction * f;
            if (bracketValue >= 0)
            {
                bracketed = true;
                break;
            }
            upper *= 2.0;
            if (!double.IsFinite(upper))
                break;
        }
        if (!bracketed)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                "Could not form a finite bracket for the universal anomaly.");

        double lower = 0.0;
        double x = upper * 0.5;
        double residualScale = sqrtMu * Math.Abs(dt);
        double tolerance = Math.Max(1.0e-13, residualScale * RelativeResidualTolerance);
        int iterations = 0;
        bool converged = false;
        for (; iterations < _iterationLimit; iterations++)
        {
            double chi = direction * x;
            if (!TryKepler(chi, a, b, alpha, r0, sqrtMu * dt,
                    out double f, out double derivative, out double radiusFromEquation))
                return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                    "Universal Kepler equation produced a non-finite value.", iterations + 1);
            double value = direction * f;
            if (Math.Abs(value) <= tolerance)
            {
                converged = true;
                iterations++;
                break;
            }

            if (value < 0)
                lower = x;
            else
            {
                upper = x;
            }

            double candidate = x - value / derivative;
            if (!double.IsFinite(candidate) || candidate <= lower || candidate >= upper ||
                !double.IsFinite(radiusFromEquation) || radiusFromEquation <= 0)
                candidate = lower + (upper - lower) * 0.5;
            if (candidate == x || candidate == lower || candidate == upper)
            {
                if (upper - lower <= 2.0e-15 * Math.Max(1.0, upper))
                {
                    converged = true;
                    iterations++;
                    break;
                }
                candidate = lower + (upper - lower) * 0.5;
            }
            x = candidate;
        }
        if (!converged)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NonConvergent,
                $"Universal anomaly solver did not converge in {_iterationLimit} iterations.", iterations);

        double solvedChi = direction * x;
        double z = alpha * solvedChi * solvedChi;
        if (!TryStumpff(z, out double c, out double s))
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                "Stumpff functions could not be evaluated in the supported numerical range.", iterations);

        double fLagrange = 1.0 - solvedChi * solvedChi / r0 * c;
        double gLagrange = dt - solvedChi * solvedChi * solvedChi / sqrtMu * s;
        Vector3D position = initial.PositionMeters * fLagrange + initial.VelocityMetersPerSecond * gLagrange;
        double radius = Norm(position);
        if (!IsFinite(position) || !double.IsFinite(radius) || radius <= 0)
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                "Propagated position is non-finite or singular.", iterations);

        double fdot = sqrtMu / (radius * r0) * (alpha * solvedChi * solvedChi * solvedChi * s - solvedChi);
        double gdot = 1.0 - solvedChi * solvedChi / radius * c;
        Vector3D velocity = initial.PositionMeters * fdot + initial.VelocityMetersPerSecond * gdot;
        if (!IsFinite(velocity))
            return TwoBodyPropagationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                "Propagated velocity is non-finite.", iterations);

        var state = new OrbitState(targetEtSeconds, position, velocity);
        return new TwoBodyPropagationResult(TwoBodyPropagationStatus.Success, state,
            Classify(initial.PositionMeters, initial.VelocityMetersPerSecond, _mu), iterations, null);
    }

    private static bool TryKepler(double chi, double a, double b, double alpha, double r0,
        double sqrtMuDt, out double f, out double derivative, out double radius)
    {
        double z = alpha * chi * chi;
        if (!TryStumpff(z, out double c, out double s))
        {
            f = derivative = radius = double.NaN;
            return false;
        }
        double chi2 = chi * chi;
        double chi3 = chi2 * chi;
        f = a * chi2 * c + b * chi3 * s + r0 * chi - sqrtMuDt;
        derivative = a * chi * (1.0 - z * s) + b * chi2 * c + r0;
        radius = derivative;
        return double.IsFinite(f) && double.IsFinite(derivative) && derivative > 0;
    }

    private static bool TryStumpff(double z, out double c, out double s)
    {
        if (!double.IsFinite(z))
        {
            c = s = double.NaN;
            return false;
        }
        if (Math.Abs(z) < SmallStumpffArgument)
        {
            double z2 = z * z;
            double z3 = z2 * z;
            double z4 = z3 * z;
            c = 0.5 - z / 24.0 + z2 / 720.0 - z3 / 40320.0 + z4 / 3628800.0;
            s = 1.0 / 6.0 - z / 120.0 + z2 / 5040.0 - z3 / 362880.0 + z4 / 39916800.0;
            return double.IsFinite(c) && double.IsFinite(s);
        }
        if (z > 0)
        {
            double root = Math.Sqrt(z);
            if (root > MaximumEllipticArgument)
            {
                c = s = double.NaN;
                return false;
            }
            double sine = Math.Sin(root * 0.5);
            c = 2.0 * sine * sine / z;
            s = (root - Math.Sin(root)) / (root * root * root);
            return double.IsFinite(c) && double.IsFinite(s);
        }

        double hyperbolicRoot = Math.Sqrt(-z);
        if (hyperbolicRoot > MaximumHyperbolicArgument)
        {
            c = s = double.NaN;
            return false;
        }
        c = 2.0 * Math.Sinh(hyperbolicRoot * 0.5) * Math.Sinh(hyperbolicRoot * 0.5) / -z;
        s = (Math.Sinh(hyperbolicRoot) - hyperbolicRoot) /
            (hyperbolicRoot * hyperbolicRoot * hyperbolicRoot);
        return double.IsFinite(c) && double.IsFinite(s);
    }

    private static ConicRegime Classify(Vector3D r, Vector3D v, double mu)
    {
        double radius = Norm(r);
        double alpha = 2.0 / radius - Dot(v, v) / mu;
        double scaled = alpha * radius;
        return Math.Abs(scaled) <= 1.0e-10
            ? ConicRegime.NearParabolic
            : scaled > 0 ? ConicRegime.Elliptical : ConicRegime.Hyperbolic;
    }

    private static double Norm(Vector3D v) => StableNorm.Three(v.X, v.Y, v.Z);
    private static double Dot(Vector3D a, Vector3D b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    private static Vector3D Cross(Vector3D a, Vector3D b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X);
    private static bool IsFinite(Vector3D v) =>
        double.IsFinite(v.X) && double.IsFinite(v.Y) && double.IsFinite(v.Z);
    private static bool IsFinite(OrbitState s) => double.IsFinite(s.EtSeconds) &&
        IsFinite(s.PositionMeters) && IsFinite(s.VelocityMetersPerSecond);
}

public sealed record SimulatedTrajectoryScenario(
    string Id,
    string CentralBodyId,
    string FrameId,
    string CoordinateOriginId,
    OrbitState InitialState,
    string GravitationalParameterIdentity,
    string ModelIdentity,
    string Provenance,
    double MaximumHorizonSeconds)
{
    public const string DemoScenarioId = "SN8-DEMO";

    public static SimulatedTrajectoryScenario CreateDemonstration(SunGravitationalParameter gm)
    {
        ArgumentNullException.ThrowIfNull(gm);
        return new SimulatedTrajectoryScenario(
            DemoScenarioId, "sun", "J2000", "sun",
            new OrbitState(0.0, new Vector3D(149_597_870_700.0, 0.0, 0.0),
                new Vector3D(0.0, 29_784.6918, 0.0)),
            gm.Identity,
            "Idealized Newtonian Sun-centered two-body point-mass propagation",
            "Deterministic simulated demonstration state at J2000; not observed spacecraft motion or a flight plan.",
            TwoBodyPropagator.MaximumPropagationHorizonSeconds);
    }

    public string? Validate()
    {
        if (string.IsNullOrWhiteSpace(Id) || Id.Length > 64)
            return "Scenario identifier must contain 1 to 64 characters.";
        if (CentralBodyId != "sun" || CoordinateOriginId != "sun" || FrameId != "J2000")
            return "SN8 currently supports only Sun-relative states with J2000-aligned axes and origin sun.";
        if (string.IsNullOrWhiteSpace(GravitationalParameterIdentity) ||
            string.IsNullOrWhiteSpace(ModelIdentity) || string.IsNullOrWhiteSpace(Provenance))
            return "Scenario model and provenance identities are required.";
        if (!double.IsFinite(MaximumHorizonSeconds) || MaximumHorizonSeconds <= 0 ||
            MaximumHorizonSeconds > TwoBodyPropagator.MaximumPropagationHorizonSeconds)
            return "Scenario horizon must be positive and within the 100 Julian-year solver limit.";
        if (!double.IsFinite(InitialState.EtSeconds) || !IsFinite(InitialState.PositionMeters) ||
            !IsFinite(InitialState.VelocityMetersPerSecond))
            return "Scenario initial state must contain finite ET, position, and velocity values.";
        double radius = StableNorm.Three(InitialState.PositionMeters.X, InitialState.PositionMeters.Y, InitialState.PositionMeters.Z);
        double speed = StableNorm.Three(InitialState.VelocityMetersPerSecond.X, InitialState.VelocityMetersPerSecond.Y, InitialState.VelocityMetersPerSecond.Z);
        if (radius < TwoBodyPropagator.MinimumInitialRadiusMeters || radius > TwoBodyPropagator.MaximumInitialRadiusMeters ||
            speed == 0 || speed > TwoBodyPropagator.MaximumInitialSpeedMetersPerSecond)
            return "Initial radius or speed lies outside the supported finite state bounds.";
        return null;
    }

    private static bool IsFinite(Vector3D v) =>
        double.IsFinite(v.X) && double.IsFinite(v.Y) && double.IsFinite(v.Z);
}

public sealed record ImpulsiveManeuver(
    string Id,
    double EtSeconds,
    Vector3D DeltaVelocityMetersPerSecond,
    string FrameId = "J2000",
    bool Enabled = true,
    string Label = "");

public enum TrajectorySampleKind
{
    Regular,
    InitialState,
    CurrentEpoch,
    ManeuverPre,
    ManeuverPost,
}

public sealed record TrajectorySample(
    string Id,
    OrbitState State,
    int SegmentIndex,
    TrajectorySampleKind Kind,
    NavigationResultStatus Quality,
    string Provenance,
    string ModelIdentity);

public sealed record ManeuverExecution(
    ImpulsiveManeuver Maneuver,
    OrbitState PreManeuverState,
    OrbitState PostManeuverState,
    double MagnitudeMetersPerSecond,
    int SegmentBefore,
    int SegmentAfter);

public sealed record TrajectorySamplingRequest(
    double CurrentEtSeconds,
    double StartEtSeconds,
    double EndEtSeconds,
    int RequestedSampleCount = 49,
    int MaximumSampleCount = 512,
    bool ProvisionalTime = false);

public sealed record SimulatedTrajectory(
    SimulatedTrajectoryScenario Scenario,
    OrbitState CurrentState,
    IReadOnlyList<TrajectorySample> Samples,
    IReadOnlyList<ManeuverExecution> Maneuvers,
    NavigationResultStatus Quality,
    string Provenance,
    string ModelIdentity,
    TimeSpan GenerationDuration);

public sealed record TrajectoryGenerationResult(
    TwoBodyPropagationStatus Status,
    SimulatedTrajectory? Trajectory,
    string? Error)
{
    public bool IsSuccess => Status == TwoBodyPropagationStatus.Success && Trajectory is not null;
    public static TrajectoryGenerationResult Failure(TwoBodyPropagationStatus status, string error) => new(status, null, error);
}

/// <summary>Bounded maneuver timeline and deterministic trajectory sampling.</summary>
public sealed class TrajectoryGenerator(TwoBodyPropagator propagator)
{
    public const int MaximumManeuvers = 8;
    public const int DefaultMaximumSamples = 512;
    public const double MaximumDeltaVelocityMetersPerSecond = 50_000.0;
    private readonly TwoBodyPropagator _propagator = propagator ?? throw new ArgumentNullException(nameof(propagator));

    public TrajectoryGenerationResult Generate(SimulatedTrajectoryScenario scenario,
        IReadOnlyList<ImpulsiveManeuver> maneuvers, TrajectorySamplingRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scenario);
        ArgumentNullException.ThrowIfNull(maneuvers);
        ArgumentNullException.ThrowIfNull(request);
        var timer = Stopwatch.StartNew();
        string? scenarioError = scenario.Validate();
        if (scenarioError is not null)
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput, scenarioError);
        if (_propagator.GravitationalParameterIdentity is { } parameterIdentity &&
            !string.Equals(scenario.GravitationalParameterIdentity, parameterIdentity, StringComparison.Ordinal))
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                "Scenario gravitational-parameter identity does not match the active propagator.");
        if (maneuvers.Count > MaximumManeuvers)
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                $"At most {MaximumManeuvers} maneuver events are allowed per scenario.");
        if (!double.IsFinite(request.CurrentEtSeconds) || !double.IsFinite(request.StartEtSeconds) ||
            !double.IsFinite(request.EndEtSeconds) || request.EndEtSeconds < request.StartEtSeconds)
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                "Current, start, and end ET values must be finite and the end must not precede the start.");
        if (request.MaximumSampleCount < 2 || request.MaximumSampleCount > DefaultMaximumSamples ||
            request.RequestedSampleCount < 2 || request.RequestedSampleCount > request.MaximumSampleCount)
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                $"Requested sample count must be 2..{DefaultMaximumSamples}, within the configured maximum.");

        double initialEt = scenario.InitialState.EtSeconds;
        foreach (double epoch in new[] { request.CurrentEtSeconds, request.StartEtSeconds, request.EndEtSeconds })
        {
            if (Math.Abs(epoch - initialEt) > scenario.MaximumHorizonSeconds)
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.HorizonExceeded,
                    $"ET {epoch:R}s is outside scenario horizon ±{scenario.MaximumHorizonSeconds:R}s from initial ET {initialEt:R}s.");
        }

        var ordered = maneuvers.OrderBy(m => m.EtSeconds).ThenBy(m => m.Id, StringComparer.Ordinal).ToArray();
        var ids = new HashSet<string>(StringComparer.Ordinal);
        for (int i = 0; i < ordered.Length; i++)
        {
            var maneuver = ordered[i];
            if (string.IsNullOrWhiteSpace(maneuver.Id) || maneuver.Id.Length > 64 || !ids.Add(maneuver.Id))
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                    "Maneuver identifiers must be unique and contain 1 to 64 characters.");
            if (!double.IsFinite(maneuver.EtSeconds) || maneuver.EtSeconds < initialEt ||
                maneuver.EtSeconds - initialEt > scenario.MaximumHorizonSeconds)
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                    $"Maneuver '{maneuver.Id}' must be on or after initial ET and within the scenario horizon.");
            if (i > 0 && maneuver.EtSeconds == ordered[i - 1].EtSeconds)
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                    $"Maneuver epochs must be unique; duplicate ET {maneuver.EtSeconds:R}s is not allowed.");
            if (maneuver.FrameId != scenario.FrameId || !IsFinite(maneuver.DeltaVelocityMetersPerSecond))
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                    $"Maneuver '{maneuver.Id}' must use {scenario.FrameId} axes and finite Δv components.");
            double magnitude = Norm(maneuver.DeltaVelocityMetersPerSecond);
            if (!double.IsFinite(magnitude) || magnitude > MaximumDeltaVelocityMetersPerSecond)
                return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.UnsupportedRegime,
                    $"Maneuver '{maneuver.Id}' magnitude exceeds {MaximumDeltaVelocityMetersPerSecond:R} m/s.");
        }

        int segment = 0;
        OrbitState timelineState = scenario.InitialState;
        int eventIndex = 0;
        var executions = new List<ManeuverExecution>(ordered.Length);
        var queryTimes = new SortedSet<double>();
        for (int i = 0; i < request.RequestedSampleCount; i++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            double fraction = (double)i / (request.RequestedSampleCount - 1);
            queryTimes.Add(i == request.RequestedSampleCount - 1
                ? request.EndEtSeconds
                : request.StartEtSeconds + (request.EndEtSeconds - request.StartEtSeconds) * fraction);
        }
        queryTimes.Add(request.CurrentEtSeconds);
        if (scenario.InitialState.EtSeconds >= request.StartEtSeconds && scenario.InitialState.EtSeconds <= request.EndEtSeconds)
            queryTimes.Add(scenario.InitialState.EtSeconds);
        foreach (var maneuver in ordered)
            if (maneuver.EtSeconds >= request.StartEtSeconds && maneuver.EtSeconds <= request.EndEtSeconds)
                queryTimes.Add(maneuver.EtSeconds);

        var states = new Dictionary<double, (OrbitState State, int Segment, TrajectorySampleKind Kind)>();
        foreach (double epoch in queryTimes)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (epoch < initialEt)
            {
                var backward = _propagator.Propagate(scenario.InitialState, epoch);
                if (!backward.IsSuccess)
                    return TrajectoryGenerationResult.Failure(backward.Status,
                        $"Propagation to ET {epoch:R}s failed: {backward.Error}");
                states[epoch] = (backward.State!.Value, 0,
                    epoch == request.CurrentEtSeconds ? TrajectorySampleKind.CurrentEpoch : TrajectorySampleKind.Regular);
                continue;
            }

            while (eventIndex < ordered.Length && ordered[eventIndex].EtSeconds <= epoch)
            {
                var maneuver = ordered[eventIndex++];
                var preResult = _propagator.Propagate(timelineState, maneuver.EtSeconds);
                if (!preResult.IsSuccess)
                    return TrajectoryGenerationResult.Failure(preResult.Status,
                        $"Propagation to maneuver '{maneuver.Id}' at ET {maneuver.EtSeconds:R}s failed: {preResult.Error}");
                OrbitState pre = preResult.State!.Value;
                OrbitState post = pre;
                int segmentBefore = segment;
                if (maneuver.Enabled)
                {
                    post = new OrbitState(maneuver.EtSeconds, pre.PositionMeters,
                        pre.VelocityMetersPerSecond + maneuver.DeltaVelocityMetersPerSecond);
                    if (!IsFinite(post.VelocityMetersPerSecond))
                        return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                            $"Maneuver '{maneuver.Id}' produced a non-finite post-impulse velocity.");
                    segment++;
                }
                executions.Add(new ManeuverExecution(maneuver, pre, post,
                    Norm(maneuver.DeltaVelocityMetersPerSecond),
                    segmentBefore, segment));
                timelineState = post;
            }

            if (timelineState.EtSeconds < epoch)
            {
                var coast = _propagator.Propagate(timelineState, epoch);
                if (!coast.IsSuccess)
                    return TrajectoryGenerationResult.Failure(coast.Status,
                        $"Propagation to ET {epoch:R}s failed: {coast.Error}");
                timelineState = coast.State!.Value;
            }
            bool atManeuver = ordered.Any(m => m.EtSeconds == epoch);
            TrajectorySampleKind kind = atManeuver ? TrajectorySampleKind.ManeuverPost :
                epoch == scenario.InitialState.EtSeconds ? TrajectorySampleKind.InitialState :
                epoch == request.CurrentEtSeconds ? TrajectorySampleKind.CurrentEpoch : TrajectorySampleKind.Regular;
            states[epoch] = (timelineState, segment, kind);
        }

        // Also calculate events after the last sample so their details and map
        // markers remain available when the viewing window is centered earlier.
        while (eventIndex < ordered.Length)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var maneuver = ordered[eventIndex++];
            var preResult = _propagator.Propagate(timelineState, maneuver.EtSeconds);
            if (!preResult.IsSuccess)
                return TrajectoryGenerationResult.Failure(preResult.Status,
                    $"Propagation to maneuver '{maneuver.Id}' at ET {maneuver.EtSeconds:R}s failed: {preResult.Error}");
            OrbitState pre = preResult.State!.Value;
            OrbitState post = maneuver.Enabled
                ? new OrbitState(maneuver.EtSeconds, pre.PositionMeters,
                    pre.VelocityMetersPerSecond + maneuver.DeltaVelocityMetersPerSecond)
                : pre;
            int before = segment;
            if (maneuver.Enabled)
                segment++;
            executions.Add(new ManeuverExecution(maneuver, pre, post,
                Norm(maneuver.DeltaVelocityMetersPerSecond),
                before, segment));
            timelineState = post;
        }

        if (!states.TryGetValue(request.CurrentEtSeconds, out var current))
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.NumericalFailure,
                "Current trajectory state was not produced by the bounded sampler.");

        var samples = new List<TrajectorySample>(states.Count + executions.Count * 2);
        foreach (var pair in states.Where(p => p.Key >= request.StartEtSeconds && p.Key <= request.EndEtSeconds)
                     .OrderBy(p => p.Key))
        {
            if (pair.Key == request.CurrentEtSeconds &&
                executions.Any(m => m.Maneuver.EtSeconds == pair.Key && m.Maneuver.Enabled))
            {
                var execution = executions.First(m => m.Maneuver.EtSeconds == pair.Key && m.Maneuver.Enabled);
                samples.Add(CreateSample(execution.Maneuver.Id + ":pre", execution.PreManeuverState,
                    execution.SegmentBefore, TrajectorySampleKind.ManeuverPre, request.ProvisionalTime));
                samples.Add(CreateSample(execution.Maneuver.Id + ":post", execution.PostManeuverState,
                    execution.SegmentAfter, TrajectorySampleKind.ManeuverPost, request.ProvisionalTime));
            }
            else
            {
                samples.Add(CreateSample($"sample:{pair.Key:R}", pair.Value.State,
                    pair.Value.Segment, pair.Value.Kind, request.ProvisionalTime));
            }
        }
        foreach (var execution in executions.Where(m =>
                     m.Maneuver.EtSeconds >= request.StartEtSeconds && m.Maneuver.EtSeconds <= request.EndEtSeconds))
        {
            if (!execution.Maneuver.Enabled)
                continue;
            if (execution.Maneuver.EtSeconds == request.CurrentEtSeconds)
                continue;
            samples.RemoveAll(s => s.State.EtSeconds == execution.Maneuver.EtSeconds &&
                s.Kind is TrajectorySampleKind.ManeuverPost or TrajectorySampleKind.Regular);
            samples.Add(CreateSample(execution.Maneuver.Id + ":pre", execution.PreManeuverState,
                execution.SegmentBefore, TrajectorySampleKind.ManeuverPre, request.ProvisionalTime));
            samples.Add(CreateSample(execution.Maneuver.Id + ":post", execution.PostManeuverState,
                execution.SegmentAfter, TrajectorySampleKind.ManeuverPost, request.ProvisionalTime));
        }
        samples = samples.OrderBy(s => s.State.EtSeconds)
            .ThenBy(s => s.Kind == TrajectorySampleKind.ManeuverPre ? 0 : 1)
            .ToList();
        if (samples.Count > request.MaximumSampleCount)
            return TrajectoryGenerationResult.Failure(TwoBodyPropagationStatus.InvalidInput,
                $"Trajectory boundaries require {samples.Count} samples, exceeding the configured cap of {request.MaximumSampleCount}.");

        timer.Stop();
        var quality = request.ProvisionalTime
            ? NavigationResultStatus.StaleData
            : NavigationResultStatus.ApproximateOrSimulated;
        string provenance = scenario.Provenance + " " + _propagator.Provenance +
            " Relative coast propagated about the Sun; placed in the requested visualization origin using DE440s celestial states. " +
            "This composition is not a self-consistent N-body integration.";
        return new TrajectoryGenerationResult(TwoBodyPropagationStatus.Success,
            new SimulatedTrajectory(scenario, current.State, samples, executions, quality,
                provenance, _propagator.ModelIdentity, timer.Elapsed), null);
    }

    private TrajectorySample CreateSample(string id, OrbitState state, int segment,
        TrajectorySampleKind kind, bool provisional) => new(id, state, segment, kind,
        provisional ? NavigationResultStatus.StaleData : NavigationResultStatus.ApproximateOrSimulated,
        _propagator.Provenance, _propagator.ModelIdentity);

    private static double Norm(Vector3D v) => StableNorm.Three(v.X, v.Y, v.Z);
    private static bool IsFinite(Vector3D v) =>
        double.IsFinite(v.X) && double.IsFinite(v.Y) && double.IsFinite(v.Z);
}

internal static class StableNorm
{
    public static double Three(double x, double y, double z) => Hypot(Hypot(x, y), z);

    private static double Hypot(double a, double b)
    {
        a = Math.Abs(a);
        b = Math.Abs(b);
        double high = Math.Max(a, b);
        double low = Math.Min(a, b);
        if (double.IsInfinity(high) || high == 0)
            return high;
        double ratio = low / high;
        return high * Math.Sqrt(1.0 + ratio * ratio);
    }
}
