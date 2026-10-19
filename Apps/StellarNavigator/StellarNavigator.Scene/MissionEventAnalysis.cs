using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;

namespace StellarNavigator.Scene;

public enum MissionEventType
{
    SimulationStart,
    Maneuver,
    ClosestApproach,
    Warning,
    SimulationEnd,
}

/// <summary>
/// One bounded event in the simulated mission timeline. Distances use metres,
/// relative speeds use metres/second, and epochs use ET/TDB seconds past J2000.
/// </summary>
public sealed record MissionEvent(
    string Id,
    MissionEventType Type,
    double EtSeconds,
    string Name,
    string? ObjectId = null,
    string? ManeuverStatus = null,
    double? DistanceMeters = null,
    double? RelativeSpeedMetersPerSecond = null,
    double? DeltaVMagnitudeMetersPerSecond = null,
    bool? IsRefined = null,
    int? SegmentIndex = null,
    string? Warning = null);

/// <summary>Bounded timeline and explicit sample-resolution metadata.</summary>
public sealed record MissionEventTimeline(
    string TargetBodyId,
    double StartEtSeconds,
    double EndEtSeconds,
    int InputSampleCount,
    double MaximumSampleIntervalSeconds,
    int RefinedApproachCount,
    bool IsApproximate,
    IReadOnlyList<MissionEvent> Events);

/// <summary>
/// Deterministic event analysis over SN8's piecewise two-body trajectory.
/// Ephemeris states are queried directly at ET in barycentric J2000 axes; no
/// target-position interpolation is performed.
/// </summary>
public sealed class MissionEventAnalyzer
{
    public const int MaximumEvents = 64;
    public const int MaximumClosestApproaches = 32;
    public const int MaximumRefinedCandidates = 16;
    public const int MaximumRefinementIterations = 64;
    public const int MaximumWarnings = 8;

    private const double SecondsPerDay = 86_400.0;
    private const double J2000Jd = 2_451_545.0;
    private const double GoldenRatioConjugate = 0.6180339887498948482;

    private readonly TwoBodyPropagator _propagator;
    private readonly IEphemerisProvider _ephemeris;
    private readonly IEtEphemerisProvider _etEphemeris;

    public MissionEventAnalyzer(TwoBodyPropagator propagator, IEphemerisProvider ephemeris)
    {
        _propagator = propagator ?? throw new ArgumentNullException(nameof(propagator));
        _ephemeris = ephemeris ?? throw new ArgumentNullException(nameof(ephemeris));
        _etEphemeris = ephemeris as IEtEphemerisProvider ??
            throw new ArgumentException("Mission-event analysis requires direct ET ephemeris queries.", nameof(ephemeris));
    }

    public MissionEventTimeline Analyze(SimulatedTrajectory trajectory, string targetBodyId,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(trajectory);
        if (string.IsNullOrWhiteSpace(targetBodyId) || targetBodyId.Length > 64)
            throw new ArgumentException("Target body id must contain 1 to 64 characters.", nameof(targetBodyId));

        var warnings = new List<MissionEvent>(MaximumWarnings);
        var warningKeys = new HashSet<string>(StringComparer.Ordinal);
        var events = new List<MissionEvent>(MaximumEvents);
        void Warn(string code, double epoch, string message, string? objectId = null)
        {
            if (warnings.Count >= MaximumWarnings || !double.IsFinite(epoch) || !warningKeys.Add(code + ":" + objectId))
                return;
            warnings.Add(new MissionEvent("warning:" + code + ":" + (objectId ?? string.Empty),
                MissionEventType.Warning, epoch, "Analysis warning", objectId, Warning: message));
        }

        var samples = trajectory.Samples ?? Array.Empty<TrajectorySample>();
        double fallbackEpoch = double.IsFinite(trajectory.Scenario.InitialState.EtSeconds)
            ? trajectory.Scenario.InitialState.EtSeconds : 0.0;
        double[] finiteEpochs = samples.Where(sample => sample is not null && double.IsFinite(sample.State.EtSeconds))
            .Select(sample => sample.State.EtSeconds).ToArray();
        double startEt = finiteEpochs.Length == 0 ? fallbackEpoch : finiteEpochs.Min();
        double endEt = finiteEpochs.Length == 0 ? fallbackEpoch : finiteEpochs.Max();
        events.Add(new MissionEvent("simulation:start", MissionEventType.SimulationStart,
            startEt, "Simulation start"));
        events.Add(new MissionEvent("simulation:end", MissionEventType.SimulationEnd,
            endEt, "Simulation end"));

        foreach (var execution in trajectory.Maneuvers
                     .Where(item => double.IsFinite(item.Maneuver.EtSeconds) &&
                         item.Maneuver.EtSeconds >= startEt && item.Maneuver.EtSeconds <= endEt)
                     .OrderBy(item => item.Maneuver.EtSeconds)
                     .ThenBy(item => item.Maneuver.Id, StringComparer.Ordinal))
        {
            bool applied = execution.Maneuver.Enabled && execution.SegmentAfter != execution.SegmentBefore;
            string label = string.IsNullOrWhiteSpace(execution.Maneuver.Label)
                ? execution.Maneuver.Id : execution.Maneuver.Label;
            events.Add(new MissionEvent("maneuver:" + execution.Maneuver.Id,
                MissionEventType.Maneuver, execution.Maneuver.EtSeconds,
                "Maneuver " + label, execution.Maneuver.Id,
                applied ? "Applied" : "Disabled",
                DeltaVMagnitudeMetersPerSecond: execution.MagnitudeMetersPerSecond));
        }

        bool approximate = trajectory.Quality != NavigationResultStatus.ValidatedReference;
        if (trajectory.Quality == NavigationResultStatus.StaleData ||
            samples.Any(sample => sample?.Quality == NavigationResultStatus.StaleData))
        {
            Warn("provisional-time", startEt,
                "The trajectory uses provisional time quality; event epochs and results are not fully qualified.");
        }
        Warn("simulation-model", startEt,
            "Results use the idealized SN8 Sun-centered two-body model and are not flight-certified navigation predictions.");

        var cache = new Dictionary<double, TargetMotion>();
        bool targetAvailable = ValidateTargetCoverage(targetBodyId, startEt, endEt, out string? coverageWarning);
        if (!targetAvailable || coverageWarning is not null)
            Warn("ephemeris-coverage", startEt, coverageWarning ?? "Target ephemeris coverage is unavailable.", targetBodyId);

        int usableSampleCount = 0;
        double maximumInterval = 0.0;
        var candidates = new List<ApproachCandidate>();
        if (samples.Count < 2)
        {
            Warn("insufficient-samples", startEt,
                "At least two trajectory samples are required to resolve closest approaches.");
        }
        else if (targetAvailable)
        {
            foreach (var segment in samples.GroupBy(sample => sample.SegmentIndex).OrderBy(group => group.Key))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var run = new List<DistancePoint>();
                double priorEt = double.NegativeInfinity;

                void CompleteRun()
                {
                    if (run.Count == 0)
                        return;
                    if (run.Count < 2)
                    {
                        Warn("insufficient-segment-samples", run[0].Sample.State.EtSeconds,
                            "A continuous trajectory segment has fewer than two valid samples; no closest-approach minimum was inferred.", targetBodyId);
                        run.Clear();
                        return;
                    }
                    usableSampleCount += run.Count;
                    for (int index = 1; index < run.Count; index++)
                        maximumInterval = Math.Max(maximumInterval,
                            run[index].Sample.State.EtSeconds - run[index - 1].Sample.State.EtSeconds);
                    AddRunCandidates(run, segment.Key, candidates);
                    run.Clear();
                }

                foreach (var sample in segment.OrderBy(item => item.State.EtSeconds)
                             .ThenBy(item => item.Kind == TrajectorySampleKind.ManeuverPre ? 0 : 1)
                             .ThenBy(item => item.Id, StringComparer.Ordinal))
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    if (!IsFinite(sample.State) || !IsUsableQuality(sample.Quality))
                    {
                        Warn("invalid-trajectory-sample", double.IsFinite(sample.State.EtSeconds)
                                ? sample.State.EtSeconds : startEt,
                            "A non-finite or unsupported trajectory sample was skipped; adjacent samples were not joined.");
                        CompleteRun();
                        priorEt = double.NegativeInfinity;
                        continue;
                    }
                    if (sample.Quality == NavigationResultStatus.StaleData)
                        approximate = true;
                    if (sample.State.EtSeconds <= priorEt)
                    {
                        Warn("duplicate-or-unsorted-epoch", sample.State.EtSeconds,
                            "A duplicate or non-increasing epoch split the trajectory segment.");
                        CompleteRun();
                    }
                    priorEt = sample.State.EtSeconds;

                    if (!TryMeasure(sample.State, targetBodyId, cache,
                            out double distance, out double relativeSpeed, out string? error))
                    {
                        Warn("ephemeris-unavailable", sample.State.EtSeconds,
                            error ?? "Target or Sun ephemeris could not be evaluated at this epoch.", targetBodyId);
                        CompleteRun();
                        continue;
                    }
                    run.Add(new DistancePoint(sample, distance, relativeSpeed));
                }
                CompleteRun();
            }
        }

        if (usableSampleCount < 2)
        {
            Warn("insufficient-valid-samples", startEt,
                "Fewer than two valid, ephemeris-covered samples remain; closest approach was not calculated.", targetBodyId);
        }
        else
        {
            if (maximumInterval > 0)
                Warn("sample-resolution", startEt,
                    $"Closest-approach search is limited by a maximum {maximumInterval:R} s sample gap; unbracketed or smaller multiple minima may be missed.", targetBodyId);

            var distinctCandidates = candidates.GroupBy(candidate => candidate.EtSeconds)
                .Select(group =>
                {
                    var selected = group.OrderBy(candidate => candidate.DistanceMeters)
                        .ThenBy(candidate => candidate.SegmentIndex).First();
                    return group.Count() > 1 && group.Any(candidate => candidate.IsManeuverBoundary)
                        ? selected with
                        {
                            SegmentIndex = -1,
                            CanRefine = false,
                            IsManeuverBoundary = true,
                        }
                        : selected;
                }).ToList();
            var nearest = distinctCandidates.OrderBy(candidate => candidate.DistanceMeters)
                .ThenBy(candidate => candidate.EtSeconds)
                .ThenBy(candidate => candidate.SegmentIndex)
                .Take(MaximumClosestApproaches).ToList();
            if (distinctCandidates.Count > nearest.Count)
                Warn("approach-cap", startEt,
                    $"The bounded timeline reports the {nearest.Count} nearest sampled local minima; {distinctCandidates.Count - nearest.Count} additional minima were omitted.",
                    targetBodyId);

            var toRefine = nearest.Where(candidate => candidate.CanRefine)
                .OrderBy(candidate => candidate.DistanceMeters)
                .ThenBy(candidate => candidate.EtSeconds)
                .ThenBy(candidate => candidate.SegmentIndex)
                .Take(MaximumRefinedCandidates).ToArray();
            var refined = new Dictionary<ApproachCandidate, RefinedApproach>();
            foreach (var candidate in toRefine)
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (!TryRefine(candidate, targetBodyId, cache, cancellationToken, out var result, out string? error))
                {
                    Warn("refinement-unavailable", candidate.EtSeconds,
                        error ?? "Closest-approach refinement was unavailable; sampled minimum retained.", targetBodyId);
                    continue;
                }
                refined[candidate] = result;
            }
            if (nearest.Count(candidate => candidate.CanRefine) > toRefine.Length)
                Warn("refinement-cap", startEt,
                    $"Refinement is capped at {MaximumRefinedCandidates} local minima; remaining approach epochs are sampled values.", targetBodyId);

            foreach (var candidate in nearest.OrderBy(candidate => candidate.EtSeconds)
                         .ThenBy(candidate => candidate.SegmentIndex))
            {
                bool hasRefinement = refined.TryGetValue(candidate, out var result);
                bool wasRefined = hasRefinement && result.IsRefined;
                double epoch = hasRefinement ? result.EtSeconds : candidate.EtSeconds;
                double distance = hasRefinement ? result.DistanceMeters : candidate.DistanceMeters;
                double? speed = hasRefinement ? result.RelativeSpeedMetersPerSecond : candidate.RelativeSpeedMetersPerSecond;
                if (candidate.IsManeuverBoundary)
                    speed = null; // velocity is one-sided across an ideal impulse.
                events.Add(new MissionEvent(
                    $"closest:{targetBodyId}:{candidate.SegmentIndex}:{candidate.EtSeconds:R}",
                    MissionEventType.ClosestApproach, epoch,
                    $"Closest approach to {targetBodyId}", targetBodyId,
                    DistanceMeters: distance,
                    RelativeSpeedMetersPerSecond: speed,
                    IsRefined: wasRefined,
                    SegmentIndex: candidate.IsManeuverBoundary ? null : candidate.SegmentIndex));
            }
        }

        events.AddRange(warnings);
        events = events.OrderBy(item => item.EtSeconds)
            .ThenBy(item => TieBreakOrder(item.Type))
            .ThenBy(item => item.ObjectId ?? string.Empty, StringComparer.Ordinal)
            .ThenBy(item => item.Id, StringComparer.Ordinal)
            .Take(MaximumEvents).ToList();

        return new MissionEventTimeline(targetBodyId, startEt, endEt, samples.Count,
            maximumInterval, events.Count(item => item.Type == MissionEventType.ClosestApproach && item.IsRefined == true),
            approximate, events);
    }

    private bool ValidateTargetCoverage(string targetBodyId, double startEt, double endEt,
        out string? warning)
    {
        warning = null;
        if (!_ephemeris.IsBodySupported(targetBodyId))
        {
            warning = $"Target body '{targetBodyId}' is not supported by the loaded ephemeris.";
            return false;
        }
        foreach (string bodyId in targetBodyId == "sun" ? new[] { "sun" } : new[] { "sun", targetBodyId })
        {
            var coverage = _ephemeris.GetCoverage(bodyId);
            if (coverage is null || coverage.Value.Start.OriginalScale != TimeScale.Tdb ||
                coverage.Value.End.OriginalScale != TimeScale.Tdb)
            {
                warning = $"TDB coverage metadata is unavailable for '{bodyId}'; no closest-approach analysis was produced.";
                return false;
            }
            double start = (coverage.Value.Start.JdUtc - J2000Jd) * SecondsPerDay;
            double end = (coverage.Value.End.JdUtc - J2000Jd) * SecondsPerDay;
            if (!double.IsFinite(start) || !double.IsFinite(end) || end < start)
            {
                warning = $"Ephemeris coverage metadata for '{bodyId}' is invalid.";
                return false;
            }
            if (startEt < start || endEt > end)
            {
                warning = $"The requested trajectory window extends beyond '{bodyId}' ephemeris coverage [{start:R}, {end:R}] s TDB; unsupported epochs are omitted without extrapolation.";
                // Partial overlap is still analyzed sample-by-sample. Return true so gaps become explicit breaks.
            }
        }
        return true;
    }

    private bool TryMeasure(OrbitState spacecraft, string targetBodyId,
        Dictionary<double, TargetMotion> cache, out double distance, out double relativeSpeed, out string? error)
    {
        distance = relativeSpeed = double.NaN;
        error = null;
        if (!TryGetTargetMotion(spacecraft.EtSeconds, targetBodyId, cache, out TargetMotion target, out error))
            return false;
        if (!TrySubtract(spacecraft.PositionMeters, target.PositionFromSunMeters, out Vector3D relativePosition) ||
            !TrySubtract(spacecraft.VelocityMetersPerSecond, target.VelocityFromSunMetersPerSecond,
                out Vector3D relativeVelocity))
        {
            error = "Relative position or velocity overflowed the supported binary64 range.";
            return false;
        }
        distance = StableNorm(relativePosition);
        relativeSpeed = StableNorm(relativeVelocity);
        if (!double.IsFinite(distance) || !double.IsFinite(relativeSpeed))
        {
            error = "Relative distance or speed is non-finite and was rejected.";
            return false;
        }
        return true;
    }

    private bool TryGetTargetMotion(double et, string targetBodyId,
        Dictionary<double, TargetMotion> cache, out TargetMotion target, out string? error)
    {
        if (cache.TryGetValue(et, out target))
        {
            error = null;
            return true;
        }
        if (!TryGetBarycentricState("sun", et, out var sun, out error))
        {
            target = default;
            return false;
        }
        var body = targetBodyId == "sun" ? sun : default;
        if (targetBodyId != "sun" && !TryGetBarycentricState(targetBodyId, et, out body, out error))
        {
            target = default;
            return false;
        }
        if (!TrySubtract(body.Position, sun.Position, out var targetPosition) ||
            !TrySubtract(body.Velocity, sun.Velocity, out var targetVelocity))
        {
            target = default;
            error = $"'{targetBodyId}' relative to Sun overflowed the supported binary64 range.";
            return false;
        }
        target = new TargetMotion(targetPosition, targetVelocity);
        cache[et] = target;
        error = null;
        return true;
    }

    private bool TryGetBarycentricState(string bodyId, double et,
        out (Vector3D Position, Vector3D Velocity) state, out string? error)
    {
        state = default;
        error = null;
        NavigationResult<(Vector3D Position, Vector3D Velocity)> result;
        try
        {
            result = _etEphemeris.GetStateAtEt(bodyId, new EtSeconds(et), ReferenceFrame.Barycentric);
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidDataException or ArithmeticException)
        {
            error = $"{bodyId} ephemeris failed at ET {et:R}s: {ex.Message}";
            return false;
        }
        if (!result.IsSuccess)
        {
            error = $"{bodyId} ephemeris is unavailable at ET {et:R}s [{result.Status}]: {result.ErrorMessage}";
            return false;
        }
        if (result.Frame is null || !result.Frame.Value.Equals(ReferenceFrame.Barycentric))
        {
            error = $"{bodyId} ephemeris returned an unverified or incompatible coordinate frame.";
            return false;
        }
        if (result.Epoch is { } epoch)
        {
            double resultEt = (epoch.JdUtc - J2000Jd) * SecondsPerDay;
            if (epoch.OriginalScale != TimeScale.Tdb || !double.IsFinite(resultEt) ||
                Math.Abs(resultEt - et) > 1e-3)
            {
                error = $"{bodyId} ephemeris returned an incompatible epoch for ET {et:R}s.";
                return false;
            }
        }
        state = result.Value!;
        if (!IsFinite(state.Position) || !IsFinite(state.Velocity))
        {
            error = $"{bodyId} ephemeris returned non-finite SI state values.";
            return false;
        }
        return true;
    }

    private void AddRunCandidates(List<DistancePoint> run, int segmentIndex,
        List<ApproachCandidate> destination)
    {
        if (run[0].DistanceMeters <= run[1].DistanceMeters)
            destination.Add(ApproachCandidate.Boundary(run[0], segmentIndex));
        for (int index = 1; index < run.Count - 1; index++)
        {
            double current = run[index].DistanceMeters;
            if (current <= run[index - 1].DistanceMeters && current <= run[index + 1].DistanceMeters &&
                (current < run[index - 1].DistanceMeters || current < run[index + 1].DistanceMeters))
            {
                destination.Add(ApproachCandidate.Interior(run[index - 1], run[index], run[index + 1], segmentIndex));
            }
        }
        if (run[^1].DistanceMeters <= run[^2].DistanceMeters)
            destination.Add(ApproachCandidate.Boundary(run[^1], segmentIndex));
    }

    private bool TryRefine(ApproachCandidate candidate, string targetBodyId,
        Dictionary<double, TargetMotion> cache, CancellationToken cancellationToken,
        out RefinedApproach result, out string? error)
    {
        result = default;
        error = null;
        double left = candidate.LeftEtSeconds;
        double right = candidate.RightEtSeconds;
        if (!(left < candidate.EtSeconds && candidate.EtSeconds < right))
            return false;

        OrbitState anchor = candidate.AnchorState;
        bool TryAt(double et, out double distance, out double speed, out string? failure)
        {
            var propagated = _propagator.Propagate(anchor, et);
            if (!propagated.IsSuccess)
            {
                distance = speed = double.NaN;
                failure = $"Trajectory refinement failed at ET {et:R}s [{propagated.Status}]: {propagated.Error}";
                return false;
            }
            return TryMeasure(propagated.State!.Value, targetBodyId, cache,
                out distance, out speed, out failure);
        }

        double x1 = right - GoldenRatioConjugate * (right - left);
        double x2 = left + GoldenRatioConjugate * (right - left);
        if (!TryAt(x1, out double f1, out _, out error) ||
            !TryAt(x2, out double f2, out _, out error))
            return false;
        for (int iteration = 0; iteration < MaximumRefinementIterations; iteration++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            double middle = left + (right - left) * 0.5;
            double tolerance = Math.Max(1e-6, Math.Abs(middle) * 2e-15);
            if (right - left <= tolerance || x1 == left || x2 == right || x1 == x2)
                break;
            if (f1 <= f2)
            {
                right = x2;
                x2 = x1;
                f2 = f1;
                x1 = right - GoldenRatioConjugate * (right - left);
                if (!TryAt(x1, out f1, out _, out error))
                    return false;
            }
            else
            {
                left = x1;
                x1 = x2;
                f1 = f2;
                x2 = left + GoldenRatioConjugate * (right - left);
                if (!TryAt(x2, out f2, out _, out error))
                    return false;
            }
        }
        double epoch = left + (right - left) * 0.5;
        if (!TryAt(epoch, out double distanceResult, out double speedResult, out error))
            return false;
        result = distanceResult < candidate.DistanceMeters
            ? new RefinedApproach(epoch, distanceResult, speedResult, true)
            : new RefinedApproach(candidate.EtSeconds, candidate.DistanceMeters,
                candidate.RelativeSpeedMetersPerSecond, false);
        return true;
    }

    private static int TieBreakOrder(MissionEventType type) => type switch
    {
        MissionEventType.SimulationStart => 0,
        MissionEventType.Maneuver => 1,
        MissionEventType.ClosestApproach => 2,
        MissionEventType.Warning => 3,
        MissionEventType.SimulationEnd => 4,
        _ => 5,
    };

    private static bool IsUsableQuality(NavigationResultStatus quality) => quality is
        NavigationResultStatus.ValidatedReference or NavigationResultStatus.ApproximateOrSimulated or
        NavigationResultStatus.StaleData;

    private static bool IsFinite(OrbitState state) => double.IsFinite(state.EtSeconds) &&
        IsFinite(state.PositionMeters) && IsFinite(state.VelocityMetersPerSecond);

    private static bool IsFinite(Vector3D value) => double.IsFinite(value.X) &&
        double.IsFinite(value.Y) && double.IsFinite(value.Z);

    private static bool TrySubtract(Vector3D left, Vector3D right, out Vector3D result)
    {
        result = left - right;
        return IsFinite(result);
    }

    private static double StableNorm(Vector3D value)
    {
        double scale = Math.Max(Math.Abs(value.X), Math.Max(Math.Abs(value.Y), Math.Abs(value.Z)));
        if (scale == 0)
            return 0;
        if (!double.IsFinite(scale))
            return double.NaN;
        double x = value.X / scale;
        double y = value.Y / scale;
        double z = value.Z / scale;
        return scale * Math.Sqrt(x * x + y * y + z * z);
    }

    private readonly record struct TargetMotion(Vector3D PositionFromSunMeters, Vector3D VelocityFromSunMetersPerSecond);
    private sealed record DistancePoint(TrajectorySample Sample, double DistanceMeters,
        double RelativeSpeedMetersPerSecond);
    private readonly record struct RefinedApproach(double EtSeconds, double DistanceMeters,
        double RelativeSpeedMetersPerSecond, bool IsRefined);

    private sealed record ApproachCandidate(
        double EtSeconds,
        double DistanceMeters,
        double RelativeSpeedMetersPerSecond,
        int SegmentIndex,
        double LeftEtSeconds,
        double RightEtSeconds,
        OrbitState AnchorState,
        bool CanRefine,
        bool IsManeuverBoundary)
    {
        public static ApproachCandidate Boundary(DistancePoint point, int segmentIndex) => new(
            point.Sample.State.EtSeconds, point.DistanceMeters, point.RelativeSpeedMetersPerSecond,
            segmentIndex, point.Sample.State.EtSeconds, point.Sample.State.EtSeconds,
            point.Sample.State, false,
            point.Sample.Kind is TrajectorySampleKind.ManeuverPre or TrajectorySampleKind.ManeuverPost);

        public static ApproachCandidate Interior(DistancePoint left, DistancePoint point,
            DistancePoint right, int segmentIndex) => new(
                point.Sample.State.EtSeconds, point.DistanceMeters, point.RelativeSpeedMetersPerSecond,
                segmentIndex, left.Sample.State.EtSeconds, right.Sample.State.EtSeconds,
                point.Sample.State, true,
                point.Sample.Kind is TrajectorySampleKind.ManeuverPre or TrajectorySampleKind.ManeuverPost);
    }
}
