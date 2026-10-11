using System.Security.Cryptography;
using System.Text.Json;
using Astronomy.Core;
using Navigation.Core;

namespace Astronomy.Ephemeris;

/// <summary>
/// Continuous ephemeris provider evaluating genuine DE440s SPK Type 2
/// Chebyshev records in pure managed code (no CSPICE/Python at runtime).
///
/// Conventions (SN2-compatible): J2000/ICRF-compatible inertial axes, TDB
/// ephemeris time, Solar System barycenter origin, geometric states without
/// light-time or stellar-aberration corrections, SI internals (m, m/s).
/// Polynomial evaluation itself stays in the kernel's native km so the only
/// rounding is the single final km-to-m conversion.
///
/// A result with <see cref="NavigationResultStatus.ValidatedReference"/>
/// means agreement with an independent CSPICE reference within documented
/// numerical tolerances; it is NOT a flight-navigation certification.
/// </summary>
public sealed class ChebyshevEphemerisProvider : IEphemerisProvider
{
    /// <summary>
    /// Trusted manifest digest of the shipped Chebyshev dataset file.
    /// The dataset is verified against this constant on load — never against
    /// a checksum stored beside the data.
    /// </summary>
    public const string ExpectedDatasetSha256 =
        "a26de29eecd7df8b349221e630328d8f45bdf588c65e916a6593b2e83c1a9dc2";

    public const double J2000Jd = 2451545.0;
    public const double SecondsPerDay = 86400.0;

    /// <summary>Domain clamp slack for record-boundary epochs (floating-point record selection).</summary>
    private const double DomainSlack = 1e-9;

    private readonly Dictionary<string, ChebyshevSegmentData> _segmentsByBody;
    private readonly ChebyshevDatasetMetadata _metadata;

    public string DataSource { get; }
    public ReferenceFrame NativeFrame { get; }
    public ChebyshevDatasetMetadata Metadata => _metadata;
    public string Sha256 { get; }
    public int TotalRecords { get; }

    public ChebyshevEphemerisProvider(string jsonPath, string? expectedSha256 = ExpectedDatasetSha256)
    {
        if (string.IsNullOrWhiteSpace(jsonPath))
            throw new ArgumentException("Dataset path must be provided.", nameof(jsonPath));

        Sha256 = ComputeSha256(jsonPath);
        if (expectedSha256 is not null &&
            !string.Equals(Sha256, expectedSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(
                $"Chebyshev dataset checksum mismatch: file {Sha256} does not match trusted manifest {expectedSha256}.");

        ChebyshevDataset data;
        try
        {
            var json = File.ReadAllText(jsonPath);
            data = JsonSerializer.Deserialize<ChebyshevDataset>(json)
                ?? throw new InvalidDataException("Failed to deserialize Chebyshev ephemeris dataset.");
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Chebyshev dataset is not valid JSON: {ex.Message}", ex);
        }

        _metadata = data.Metadata;
        ValidateDataset(data);

        _segmentsByBody = new Dictionary<string, ChebyshevSegmentData>(StringComparer.Ordinal);
        foreach (var seg in data.Segments)
            _segmentsByBody[seg.BodyId] = seg;

        ValidateCenterChains();

        TotalRecords = data.Segments.Sum(s => s.RecordCount);
        DataSource = $"{_metadata.Source} (continuous Type-2 Chebyshev)";
        NativeFrame = ReferenceFrame.Barycentric;
    }

    private static void ValidateDataset(ChebyshevDataset data)
    {
        if (data.Segments.Count == 0)
            throw new InvalidDataException("Chebyshev dataset contains no segments.");

        int total = 0;
        foreach (var seg in data.Segments)
        {
            if (string.IsNullOrWhiteSpace(seg.BodyId))
                throw new InvalidDataException("Chebyshev segment is missing its body id.");
            if (seg.SegmentType != 2)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' has unsupported SPK type {seg.SegmentType} (only Type 2 supported).");
            if (seg.FrameId != 1)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' has unsupported frame id {seg.FrameId} (only J2000/frame 1 supported).");
            if (seg.Degree < 0)
                throw new InvalidDataException($"Segment '{seg.BodyId}' has invalid degree {seg.Degree}.");
            if (seg.RecordCount <= 0)
                throw new InvalidDataException($"Segment '{seg.BodyId}' has invalid record count {seg.RecordCount}.");
            if (!double.IsFinite(seg.RecordIntervalSeconds) || seg.RecordIntervalSeconds <= 0.0)
                throw new InvalidDataException($"Segment '{seg.BodyId}' has invalid record interval.");
            if (!double.IsFinite(seg.InitEt))
                throw new InvalidDataException($"Segment '{seg.BodyId}' has non-finite INIT epoch.");
            if (seg.Records.Count != seg.RecordCount)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' record array count {seg.Records.Count} does not match metadata {seg.RecordCount}.");

            int expectedLength = 2 + 3 * (seg.Degree + 1);
            for (int i = 0; i < seg.Records.Count; i++)
            {
                var rec = seg.Records[i];
                if (rec.Length != expectedLength)
                    throw new InvalidDataException(
                        $"Segment '{seg.BodyId}' record {i} length {rec.Length} does not match degree {seg.Degree} (expected {expectedLength}).");
                if (!double.IsFinite(rec[0]) || !double.IsFinite(rec[1]) || rec[1] <= 0.0)
                    throw new InvalidDataException(
                        $"Segment '{seg.BodyId}' record {i} has invalid midpoint/radius.");
                for (int k = 2; k < rec.Length; k++)
                {
                    if (!double.IsFinite(rec[k]))
                        throw new InvalidDataException(
                            $"Segment '{seg.BodyId}' record {i} has non-finite coefficient [{k}].");
                }
            }
            total += seg.RecordCount;
        }

        if (data.Metadata.TotalRecords != 0 && data.Metadata.TotalRecords != total)
            throw new InvalidDataException(
                $"Chebyshev dataset record total {total} does not match metadata {data.Metadata.TotalRecords}.");
    }

    /// <summary>
    /// Every segment center must resolve to the SSB (directly or transitively)
    /// through loaded segments; cycles and missing ancestors are load errors,
    /// never silent fabrications at query time.
    /// </summary>
    private void ValidateCenterChains()
    {
        var byTarget = _segmentsByBody.Values.ToDictionary(s => s.TargetId);
        foreach (var seg in _segmentsByBody.Values)
        {
            var seen = new HashSet<int> { seg.TargetId };
            int center = seg.CenterId;
            while (center != 0)
            {
                if (!seen.Add(center))
                    throw new InvalidDataException(
                        $"Segment '{seg.BodyId}' has a cyclic center chain at NAIF id {center}.");
                if (!byTarget.TryGetValue(center, out var centerSeg))
                    throw new InvalidDataException(
                        $"Segment '{seg.BodyId}' requires missing center segment NAIF id {center}.");
                center = centerSeg.CenterId;
            }
        }
    }

    private static string ComputeSha256(string filePath)
    {
        using var sha = SHA256.Create();
        using var stream = File.OpenRead(filePath);
        return Convert.ToHexString(sha.ComputeHash(stream)).ToLowerInvariant();
    }

    /// <summary>Ephemeris time (seconds past J2000 TDB) for a provider epoch.</summary>
    public static double ToEphemerisSeconds(AstroTime epoch)
    {
        if (!double.IsFinite(epoch.JdUtc))
            throw new ArgumentException("Epoch JD must be finite.");
        double jdTdb = epoch.OriginalScale == TimeScale.Tdb
            ? epoch.JdUtc
            : TdbTime.UtcToTdb(epoch).JdUtc;
        return (jdTdb - J2000Jd) * SecondsPerDay;
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
        string bodyId, AstroTime epoch, ReferenceFrame targetFrame)
    {
        if (targetFrame.Kind != ReferenceFrameKind.Barycentric &&
            targetFrame.Kind != ReferenceFrameKind.IcrfJ2000)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.UnsupportedCalculation,
                $"Frame conversion to {targetFrame} is not supported (continuous states are SSB-relative J2000 geometric states).");

        if (!double.IsFinite(epoch.JdUtc))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput,
                "Epoch JD must be a finite number.");

        if (string.Equals(bodyId, "ssb", StringComparison.Ordinal))
        {
            var cov = OverallCoverage();
            if (cov is null)
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InsufficientInformation, "No coverage available.");
            if (epoch.JdUtc < cov.Value.Start.JdUtc || epoch.JdUtc > cov.Value.End.JdUtc)
                return FailureOutsideCoverage(epoch, cov.Value);
            return NavigationResult<(Vector3D, Vector3D)>.Success(
                (new Vector3D(0, 0, 0), new Vector3D(0, 0, 0)),
                NavigationResultStatus.ValidatedReference, epoch, targetFrame, DataSource);
        }

        if (!_segmentsByBody.TryGetValue(bodyId, out _))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InsufficientInformation,
                $"Body '{bodyId}' is not in the Chebyshev ephemeris dataset.");

        double et = ToEphemerisSeconds(epoch);
        var eval = EvaluateChained(bodyId, et);
        if (!eval.IsSuccess)
            return eval;

        var (posKm, velKms) = eval.Value!;
        var posM = new Vector3D(
            posKm.X * Distance.MetersPerKilometer,
            posKm.Y * Distance.MetersPerKilometer,
            posKm.Z * Distance.MetersPerKilometer);
        var velMs = new Vector3D(
            velKms.X * Distance.MetersPerKilometer,
            velKms.Y * Distance.MetersPerKilometer,
            velKms.Z * Distance.MetersPerKilometer);

        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (posM, velMs), NavigationResultStatus.ValidatedReference, epoch, targetFrame, DataSource);
    }

    private NavigationResult<(Vector3D Position, Vector3D Velocity)> EvaluateChained(string bodyId, double et)
    {
        // state(target rel SSB) = sum over chain of state(id rel center), in km.
        var posKm = new Vector3D(0, 0, 0);
        var velKms = new Vector3D(0, 0, 0);
        var visited = new HashSet<string>(StringComparer.Ordinal);
        string current = bodyId;
        while (true)
        {
            if (!visited.Add(current))
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InvalidInput,
                    $"Cyclic center chain detected at '{current}'.");
            if (string.Equals(current, "ssb", StringComparison.Ordinal))
                return NavigationResult<(Vector3D, Vector3D)>.Success(
                    (posKm, velKms), NavigationResultStatus.ValidatedReference);
            if (!_segmentsByBody.TryGetValue(current, out var seg))
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InsufficientInformation,
                    $"Missing center-chain segment for '{current}'; state cannot be composed.");
            var step = EvaluateSegment(seg, et);
            if (!step.IsSuccess)
                return step;
            posKm += step.Value!.Position;
            velKms += step.Value!.Velocity;
            current = seg.CenterBodyId;
        }
    }

    private static NavigationResult<(Vector3D Position, Vector3D Velocity)> EvaluateSegment(
        ChebyshevSegmentData seg, double et)
    {
        double end = seg.InitEt + seg.RecordCount * seg.RecordIntervalSeconds;
        if (et < seg.InitEt || et > end)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.StaleData,
                $"Epoch ET {et:F3} s is outside '{seg.BodyId}' coverage [{seg.InitEt:F3}, {end:F3}] s past J2000. No extrapolation is performed.");

        // Deterministic record selection; an epoch exactly at the end of
        // coverage selects the final record (never a missing successor).
        int index = (int)Math.Floor((et - seg.InitEt) / seg.RecordIntervalSeconds);
        if (index < 0)
            index = 0;
        if (index >= seg.RecordCount)
            index = seg.RecordCount - 1;

        double[] rec = seg.Records[index];
        double mid = rec[0];
        double radius = rec[1];
        double u;
        try
        {
            u = ChebyshevEvaluator.NormalizeTime(et, mid, radius);
        }
        catch (ArgumentException ex)
        {
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput, $"Invalid record time base: {ex.Message}");
        }

        if (u < -1.0 - DomainSlack || u > 1.0 + DomainSlack)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput,
                $"Epoch selects record {index} of '{seg.BodyId}' but normalizes to u={u:R} outside [-1, 1]; dataset may be corrupt.");
        double uc = Math.Min(1.0, Math.Max(-1.0, u));

        int nc = seg.Degree + 1;
        var cx = new ReadOnlySpan<double>(rec, 2, nc);
        var cy = new ReadOnlySpan<double>(rec, 2 + nc, nc);
        var cz = new ReadOnlySpan<double>(rec, 2 + 2 * nc, nc);
        var (x, dx) = ChebyshevEvaluator.EvaluateWithDerivative(cx, uc);
        var (y, dy) = ChebyshevEvaluator.EvaluateWithDerivative(cy, uc);
        var (z, dz) = ChebyshevEvaluator.EvaluateWithDerivative(cz, uc);

        // Chain rule: d/dt = (d/du) / radius. Coeffs in km, radius in s -> km/s.
        var posKm = new Vector3D(x, y, z);
        var velKms = new Vector3D(dx / radius, dy / radius, dz / radius);
        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (posKm, velKms), NavigationResultStatus.ValidatedReference);
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetRelativeState(
        string targetBodyId, string observerBodyId, AstroTime epoch)
    {
        var targetResult = GetState(targetBodyId, epoch, ReferenceFrame.Barycentric);
        if (!targetResult.IsSuccess)
            return targetResult;
        var observerResult = GetState(observerBodyId, epoch, ReferenceFrame.Barycentric);
        if (!observerResult.IsSuccess)
            return observerResult;

        var (targetPos, targetVel) = targetResult.Value!;
        var (observerPos, observerVel) = observerResult.Value!;
        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (targetPos - observerPos, targetVel - observerVel),
            NavigationResultStatus.ValidatedReference, epoch, ReferenceFrame.Barycentric, DataSource);
    }

    public bool IsBodySupported(string bodyId) =>
        string.Equals(bodyId, "ssb", StringComparison.Ordinal) || _segmentsByBody.ContainsKey(bodyId);

    public (AstroTime Start, AstroTime End)? GetCoverage(string bodyId)
    {
        if (string.Equals(bodyId, "ssb", StringComparison.Ordinal))
            return OverallCoverage();
        if (!_segmentsByBody.TryGetValue(bodyId, out var seg))
            return null;
        double end = seg.InitEt + seg.RecordCount * seg.RecordIntervalSeconds;
        return (new AstroTime(J2000Jd + seg.InitEt / SecondsPerDay, TimeScale.Tdb),
                new AstroTime(J2000Jd + end / SecondsPerDay, TimeScale.Tdb));
    }

    private (AstroTime Start, AstroTime End)? OverallCoverage()
    {
        if (_segmentsByBody.Count == 0)
            return null;
        double start = double.PositiveInfinity;
        double end = double.NegativeInfinity;
        foreach (var seg in _segmentsByBody.Values)
        {
            double segEnd = seg.InitEt + seg.RecordCount * seg.RecordIntervalSeconds;
            start = Math.Min(start, seg.InitEt);
            end = Math.Max(end, segEnd);
        }
        return (new AstroTime(J2000Jd + start / SecondsPerDay, TimeScale.Tdb),
                new AstroTime(J2000Jd + end / SecondsPerDay, TimeScale.Tdb));
    }

    private NavigationResult<(Vector3D, Vector3D)> FailureOutsideCoverage(
        AstroTime epoch, (AstroTime Start, AstroTime End) cov) =>
        NavigationResult<(Vector3D, Vector3D)>.Failure(
            NavigationResultStatus.StaleData,
            $"Epoch JD {epoch.JdUtc:F6} is outside coverage [{cov.Start.JdUtc:F6}, {cov.End.JdUtc:F6}]. No extrapolation is performed.");

    public List<string> SupportedBodies
    {
        get
        {
            var bodies = _segmentsByBody.Keys.ToList();
            bodies.Add("ssb");
            bodies.Sort(StringComparer.Ordinal);
            return bodies;
        }
    }

    public AstroTime CoverageStart => OverallCoverage()!.Value.Start;
    public AstroTime CoverageEnd => OverallCoverage()!.Value.End;
}
