using System.Security.Cryptography;
using Astronomy.Core;
using Astronomy.Time;
using Navigation.Core;

namespace Astronomy.Ephemeris;

/// <summary>
/// Memory-efficient continuous ephemeris provider over an SNEPH indexed
/// binary dataset (see <see cref="ChebyshevBinaryFormat"/>).
///
/// Only the provenance block and segment index are loaded at startup;
/// coefficient records are read from disk on demand and retained in a
/// bounded least-recently-used cache. The default cache budget is 4 MiB.
/// The cache stores verbatim coefficient copies and never alters the
/// scientific evaluation path: record selection, Clenshaw evaluation,
/// chain-rule velocity scaling, center chaining, and the km-to-m output
/// conversion are identical to <see cref="ChebyshevEphemerisProvider"/>,
/// so identical datasets and epochs agree with the SN3 provider.
/// (See lazy-loader equivalence tests.)
///
/// Conventions (SN3-compatible): J2000/ICRF-compatible inertial axes, TDB
/// ephemeris time, Solar System barycenter origin, geometric states without
/// light-time or stellar-aberration corrections, SI internals (m, m/s).
/// Polynomial evaluation stays in the kernel's native km; the only rounding
/// is the single final km-to-m conversion.
///
/// A result with <see cref="NavigationResultStatus.ValidatedReference"/>
/// means agreement with an independent CSPICE reference within documented
/// numerical tolerances; it is NOT a flight-navigation certification.
///
/// Thread safety: concurrent read requests are safe. A single internal
/// lock guards the shared file stream and the cache; evaluation itself is
/// pure computation over immutable record copies. Ownership: the provider
/// holds one open read handle for its lifetime; dispose it to release the
/// handle. After disposal, queries throw <see cref="ObjectDisposedException"/>.
///
/// Trust model: the file's SHA-256 is verified against the trusted manifest
/// digest at open. The open read handle keeps referring to the same file
/// data; replacing the dataset file on disk requires opening a new
/// provider instance (which re-verifies). As with the SN3 provider, do not
/// treat a constructed provider as valid for a file that was mutated
/// after the initial verification.
/// </summary>
public sealed class IndexedChebyshevEphemerisProvider : IEphemerisProvider, IEtEphemerisProvider, IDisposable
{
    /// <summary>
    /// Trusted manifest digest of the shipped SNEPH binary dataset file.
    /// The dataset is verified against this constant on load — never against
    /// a checksum stored beside the data.
    /// </summary>
    public const string ExpectedDatasetSha256 =
        "d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a";

    /// <summary>Default coefficient-cache memory budget (4 MiB).</summary>
    public const long DefaultCacheBudgetBytes = 4L * 1024L * 1024L;

    public const double J2000Jd = 2451545.0;
    public const double SecondsPerDay = 86400.0;

    /// <summary>Domain clamp slack for record-boundary epochs (floating-point record selection).</summary>
    private const double DomainSlack = 1e-9;

    private readonly object _sync = new();
    private readonly Dictionary<string, int> _segmentByBody;
    private readonly List<IndexedSegment> _segments;
    private readonly IndexedDataset _dataset;
    private readonly FileStream _stream;

    private readonly Dictionary<RecordKey, LinkedListNode<CacheEntry>> _cacheEntries = new();
    private readonly LinkedList<CacheEntry> _lru = new();
    private long _cachedBytes;
    private long _hitCount;
    private long _missCount;
    private bool _disposed;

    public string DataSource { get; }
    public ReferenceFrame NativeFrame { get; }
    public Sn4Provenance Provenance => _dataset.Provenance;
    public string ProvenanceJson => _dataset.ProvenanceJson;
    public string Sha256 { get; }
    public long TotalRecords => _dataset.TotalRecords;
    public long CacheBudgetBytes { get; }
    public string DatasetPath { get; }

    public IndexedChebyshevEphemerisProvider(
        string datasetPath,
        string? expectedSha256 = ExpectedDatasetSha256,
        long cacheBudgetBytes = DefaultCacheBudgetBytes)
    {
        if (string.IsNullOrWhiteSpace(datasetPath))
            throw new ArgumentException("Dataset path must be provided.", nameof(datasetPath));
        if (cacheBudgetBytes < 0)
            throw new ArgumentOutOfRangeException(nameof(cacheBudgetBytes), "Cache budget must be non-negative.");

        Sha256 = ComputeSha256(datasetPath);
        if (expectedSha256 is not null &&
            !string.Equals(Sha256, expectedSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(
                $"SNEPH dataset checksum mismatch: file {Sha256} does not match trusted manifest {expectedSha256}.");

        _dataset = ChebyshevBinaryFormat.ReadIndex(datasetPath);
        _segments = _dataset.Segments;
        _segmentByBody = new Dictionary<string, int>(StringComparer.Ordinal);
        for (int i = 0; i < _segments.Count; i++)
            _segmentByBody[_segments[i].BodyId] = i;

        ValidateCenterChains();

        CacheBudgetBytes = cacheBudgetBytes;
        DatasetPath = datasetPath;
        _stream = new FileStream(datasetPath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, useAsync: false);
        DataSource = $"{Provenance.Source} (indexed Type-2 Chebyshev, lazy loading)";
        NativeFrame = ReferenceFrame.Barycentric;
    }

    /// <summary>
    /// Every segment center must resolve to the SSB (directly or transitively)
    /// through loaded segments; cycles and missing ancestors are load errors,
    /// never silent fabrications at query time.
    /// </summary>
    private void ValidateCenterChains()
    {
        var byTarget = _segments.ToDictionary(s => s.TargetId);
        foreach (var seg in _segments)
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
        if (epoch.OriginalScale != TimeScale.Tdb)
            throw new ArgumentException(
                $"Ephemeris queries require a TDB epoch; got {epoch.OriginalScale}. Convert UTC through the SN5 TimeConverter.", nameof(epoch));
        return (epoch.JdUtc - J2000Jd) * SecondsPerDay;
    }

    /// <summary>Cached coefficient-record count (for diagnostics and tests).</summary>
    public int CachedRecordCount
    {
        get { lock (_sync) { ThrowIfDisposed(); return _cacheEntries.Count; } }
    }

    /// <summary>Currently retained cached coefficient bytes (for diagnostics and tests).</summary>
    public long CachedBytes
    {
        get { lock (_sync) { ThrowIfDisposed(); return _cachedBytes; } }
    }

    public long CacheHitCount
    {
        get { lock (_sync) { ThrowIfDisposed(); return _hitCount; } }
    }

    public long CacheMissCount
    {
        get { lock (_sync) { ThrowIfDisposed(); return _missCount; } }
    }

    /// <summary>Index-only segment metadata (no coefficients).</summary>
    public IReadOnlyList<IndexedSegment> Segments => _segments;

    /// <summary>Evicts all cached coefficient records.</summary>
    public void ClearCache()
    {
        lock (_sync)
        {
            ThrowIfDisposed();
            _cacheEntries.Clear();
            _lru.Clear();
            _cachedBytes = 0;
        }
    }

    public void Dispose()
    {
        lock (_sync)
        {
            if (_disposed)
                return;
            _disposed = true;
            _cacheEntries.Clear();
            _lru.Clear();
            _cachedBytes = 0;
        }
        _stream.Dispose();
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(IndexedChebyshevEphemerisProvider));
    }

    private readonly record struct RecordKey(int SegmentOrdinal, long RecordIndex);

    private sealed class CacheEntry
    {
        public RecordKey Key;
        public double[] Data = Array.Empty<double>();
        public long ByteSize;
    }

    /// <summary>
    /// Fetches one verbatim coefficient record, using the bounded LRU cache.
    /// Returns a shared immutable copy: callers must not mutate it.
    /// Throws <see cref="InvalidDataException"/> when the record cannot be
    /// read or fails per-read validation (treat as dataset corruption).
    /// </summary>
    private double[] GetRecord(int segmentOrdinal, long index)
    {
        lock (_sync)
        {
            ThrowIfDisposed();
            var key = new RecordKey(segmentOrdinal, index);
            if (_cacheEntries.TryGetValue(key, out var node))
            {
                _lru.Remove(node);
                _lru.AddFirst(node);
                _hitCount++;
                return node.Value.Data;
            }

            double[] record;
            try
            {
                record = ChebyshevBinaryFormat.ReadRecord(_stream, _segments[segmentOrdinal], index);
            }
            catch (ArgumentOutOfRangeException ex)
            {
                throw new InvalidDataException(
                    $"Segment '{_segments[segmentOrdinal].BodyId}' record {index} is out of range.", ex);
            }
            _missCount++;
            ValidateRecordContents(_segments[segmentOrdinal], index, record);

            long byteSize = (long)record.Length * 8L;
            if (byteSize <= CacheBudgetBytes)
            {
                var entry = new CacheEntry { Key = key, Data = record, ByteSize = byteSize };
                var listNode = new LinkedListNode<CacheEntry>(entry);
                _lru.AddFirst(listNode);
                _cacheEntries[key] = listNode;
                _cachedBytes += byteSize;
                while (_cachedBytes > CacheBudgetBytes && _lru.Last is not null)
                {
                    var victim = _lru.Last;
                    _lru.RemoveLast();
                    _cacheEntries.Remove(victim.Value.Key);
                    _cachedBytes -= victim.Value.ByteSize;
                }
            }
            return record;
        }
    }

    /// <summary>
    /// Per-read validation of a freshly loaded record (the index loader
    /// cannot check coefficient values without materializing them).
    /// </summary>
    private static void ValidateRecordContents(IndexedSegment seg, long index, double[] record)
    {
        if (record.Length != seg.RecordLength)
            throw new InvalidDataException(
                $"Segment '{seg.BodyId}' record {index} length {record.Length} does not match index {seg.RecordLength}.");
        if (!double.IsFinite(record[0]) || !double.IsFinite(record[1]) || record[1] <= 0.0)
            throw new InvalidDataException(
                $"Segment '{seg.BodyId}' record {index} has invalid midpoint/radius (corrupt data).");
        for (int k = 2; k < record.Length; k++)
        {
            if (!double.IsFinite(record[k]))
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' record {index} has non-finite coefficient [{k}] (corrupt data).");
        }
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
        string bodyId, AstroTime epoch, ReferenceFrame targetFrame)
    {
        if (!double.IsFinite(epoch.JdUtc))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput, "Epoch JD must be a finite number.");
        if (epoch.OriginalScale != TimeScale.Tdb)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput,
                $"Ephemeris queries require a TDB epoch; got {epoch.OriginalScale}. Convert UTC through the SN5 TimeConverter.");
        return GetStateAtEt(bodyId,
            new EtSeconds((epoch.JdUtc - J2000Jd) * SecondsPerDay), targetFrame);
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetStateAtEt(
        string bodyId, EtSeconds epoch, ReferenceFrame targetFrame)
    {
        lock (_sync) { ThrowIfDisposed(); }

        if (targetFrame.Kind != ReferenceFrameKind.Barycentric &&
            targetFrame.Kind != ReferenceFrameKind.IcrfJ2000)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.UnsupportedCalculation,
                $"Frame conversion to {targetFrame} is not supported (continuous states are SSB-relative J2000 geometric states).");

        double et = epoch.Value;
        if (!double.IsFinite(et))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput,
                "Epoch ET must be finite seconds past J2000 (TDB).");
        var epochLabel = new AstroTime(epoch.ToJulianDateTdb(), TimeScale.Tdb);

        if (string.Equals(bodyId, "ssb", StringComparison.Ordinal))
        {
            var cov = OverallCoverageEt();
            if (cov is null)
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InsufficientInformation, "No coverage available.");
            if (et < cov.Value.Start || et > cov.Value.End)
                return FailureOutsideCoverage(et, cov.Value);
            return NavigationResult<(Vector3D, Vector3D)>.Success(
                (new Vector3D(0, 0, 0), new Vector3D(0, 0, 0)),
                NavigationResultStatus.ValidatedReference, epochLabel, targetFrame, DataSource);
        }

        if (!_segmentByBody.TryGetValue(bodyId, out _))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InsufficientInformation,
                $"Body '{bodyId}' is not in the SNEPH ephemeris dataset.");

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
            (posM, velMs), NavigationResultStatus.ValidatedReference, epochLabel, targetFrame, DataSource);
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
            if (!_segmentByBody.TryGetValue(current, out int ordinal))
                return NavigationResult<(Vector3D, Vector3D)>.Failure(
                    NavigationResultStatus.InsufficientInformation,
                    $"Missing center-chain segment for '{current}'; state cannot be composed.");
            var step = EvaluateSegment(ordinal, et);
            if (!step.IsSuccess)
                return step;
            posKm += step.Value!.Position;
            velKms += step.Value!.Velocity;
            current = _segments[ordinal].CenterBodyId;
        }
    }

    private NavigationResult<(Vector3D Position, Vector3D Velocity)> EvaluateSegment(
        int ordinal, double et)
    {
        var seg = _segments[ordinal];
        double end = seg.EndEt;
        if (et < seg.InitEt || et > end)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.StaleData,
                $"Epoch ET {et:F3} s is outside '{seg.BodyId}' coverage [{seg.InitEt:F3}, {end:F3}] s past J2000. No extrapolation is performed.");

        // Deterministic record selection; an epoch exactly at the end of
        // coverage selects the final record (never a missing successor).
        long index = (long)Math.Floor((et - seg.InitEt) / seg.RecordIntervalSeconds);
        if (index < 0)
            index = 0;
        if (index >= seg.RecordCount)
            index = seg.RecordCount - 1;

        double[] rec;
        try
        {
            rec = GetRecord(ordinal, index);
        }
        catch (InvalidDataException ex)
        {
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InvalidInput,
                $"Coefficient record {index} of '{seg.BodyId}' failed validation: {ex.Message}");
        }
        catch (ObjectDisposedException)
        {
            throw;
        }

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

    public bool IsBodySupported(string bodyId)
    {
        lock (_sync) { ThrowIfDisposed(); }
        return string.Equals(bodyId, "ssb", StringComparison.Ordinal) || _segmentByBody.ContainsKey(bodyId);
    }

    public (AstroTime Start, AstroTime End)? GetCoverage(string bodyId)
    {
        lock (_sync) { ThrowIfDisposed(); }
        if (string.Equals(bodyId, "ssb", StringComparison.Ordinal))
            return OverallCoverage();
        if (!_segmentByBody.TryGetValue(bodyId, out int ordinal))
            return null;
        var seg = _segments[ordinal];
        return (new AstroTime(J2000Jd + seg.InitEt / SecondsPerDay, TimeScale.Tdb),
                new AstroTime(J2000Jd + seg.EndEt / SecondsPerDay, TimeScale.Tdb));
    }

    private (AstroTime Start, AstroTime End)? OverallCoverage()
    {
        var coverageEt = OverallCoverageEt();
        if (coverageEt is null)
            return null;
        return (new AstroTime(new EtSeconds(coverageEt.Value.Start).ToJulianDateTdb(), TimeScale.Tdb),
                new AstroTime(new EtSeconds(coverageEt.Value.End).ToJulianDateTdb(), TimeScale.Tdb));
    }

    private (double Start, double End)? OverallCoverageEt()
    {
        if (_segments.Count == 0)
            return null;
        double start = double.PositiveInfinity;
        double end = double.NegativeInfinity;
        foreach (var seg in _segments)
        {
            start = Math.Min(start, seg.InitEt);
            end = Math.Max(end, seg.EndEt);
        }
        return (start, end);
    }

    private NavigationResult<(Vector3D, Vector3D)> FailureOutsideCoverage(
        double et, (double Start, double End) cov) =>
        NavigationResult<(Vector3D, Vector3D)>.Failure(
            NavigationResultStatus.StaleData,
            $"Epoch ET {et:F6}s is outside coverage [{cov.Start:F6}, {cov.End:F6}]s past J2000 TDB. No extrapolation is performed.");

    public List<string> SupportedBodies
    {
        get
        {
            lock (_sync) { ThrowIfDisposed(); }
            var bodies = _segmentByBody.Keys.ToList();
            bodies.Add("ssb");
            bodies.Sort(StringComparer.Ordinal);
            return bodies;
        }
    }

    public AstroTime CoverageStart => OverallCoverage()!.Value.Start;
    public AstroTime CoverageEnd => OverallCoverage()!.Value.End;
}
