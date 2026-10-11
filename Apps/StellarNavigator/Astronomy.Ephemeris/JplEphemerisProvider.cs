using System.Security.Cryptography;
using System.Text.Json;
using Astronomy.Core;
using Navigation.Core;

namespace Astronomy.Ephemeris;

public sealed class JplEphemerisProvider : IEphemerisProvider
{
    private readonly JplEphemerisData _data;
    private readonly Dictionary<string, List<JplEphemerisEntry>> _entriesByBody;
    private readonly AstroTime _coverageStart;
    private readonly AstroTime _coverageEnd;
    private readonly double _epochToleranceDays;

    public string DataSource { get; }
    public ReferenceFrame NativeFrame { get; }
    public JplEphemerisMetadata Metadata { get; }
    public string Sha256 { get; }

    public JplEphemerisProvider(string jsonPath, double epochToleranceDays = 0.01)
    {
        _epochToleranceDays = epochToleranceDays;
        var json = File.ReadAllText(jsonPath);
        Sha256 = ComputeSha256(jsonPath);

        _data = JsonSerializer.Deserialize<JplEphemerisData>(json)
            ?? throw new InvalidDataException("Failed to deserialize JPL ephemeris dataset");

        Metadata = _data.Metadata;
        ValidateStructure();

        _entriesByBody = new Dictionary<string, List<JplEphemerisEntry>>(StringComparer.Ordinal);
        foreach (var entry in _data.Entries)
        {
            if (!_entriesByBody.TryGetValue(entry.BodyId, out var list))
            {
                list = new List<JplEphemerisEntry>();
                _entriesByBody[entry.BodyId] = list;
            }
            list.Add(entry);
        }

        foreach (var list in _entriesByBody.Values)
        {
            list.Sort((a, b) => a.Jd.CompareTo(b.Jd));
        }

        var allJds = _data.Entries.Select(e => e.Jd).ToList();
        allJds.Sort();
        _coverageStart = AstroTime.FromJdUtc(allJds.First());
        _coverageEnd = AstroTime.FromJdUtc(allJds.Last());

        DataSource = Metadata.Source;
        NativeFrame = ReferenceFrame.Barycentric;
    }

    private void ValidateStructure()
    {
        if (_data.Entries.Count == 0)
            throw new InvalidDataException("JPL ephemeris dataset contains no entries");

        if (Metadata.TotalEntries != 0 && Metadata.TotalEntries != _data.Entries.Count)
            throw new InvalidDataException(
                $"JPL ephemeris entry count {_data.Entries.Count} does not match metadata total {Metadata.TotalEntries}");

        foreach (var entry in _data.Entries)
        {
            if (Metadata.Bodies.TryGetValue(entry.BodyId, out var info) && info.NaifId != entry.NaifId)
                throw new InvalidDataException(
                    $"NAIF ID mismatch for body '{entry.BodyId}': entry {entry.NaifId}, metadata {info.NaifId}");
        }
    }

    private string ComputeSha256(string filePath)
    {
        using var sha = SHA256.Create();
        using var stream = File.OpenRead(filePath);
        var hash = sha.ComputeHash(stream);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
        string bodyId, AstroTime epoch, ReferenceFrame targetFrame)
    {
        if (!_entriesByBody.TryGetValue(bodyId, out var entries))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InsufficientInformation,
                $"Body '{bodyId}' is not in the JPL ephemeris dataset");

        if (epoch.JdUtc < _coverageStart.JdUtc || epoch.JdUtc > _coverageEnd.JdUtc)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.StaleData,
                $"Epoch JD {epoch.JdUtc:F6} is outside coverage [{_coverageStart.JdUtc:F6}, {_coverageEnd.JdUtc:F6}]");

        var entry = FindExactEpoch(entries, epoch.JdUtc);
        if (entry is null)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.StaleData,
                $"Epoch JD {epoch.JdUtc:F6} does not match any sample in the dataset (sample interval: {Metadata.SampleIntervalDays} days)");

        if (targetFrame.Kind != ReferenceFrameKind.Barycentric && targetFrame.Kind != ReferenceFrameKind.IcrfJ2000)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.UnsupportedCalculation,
                $"Frame conversion to {targetFrame} is not supported");

        var posM = new Vector3D(
            entry.Position.X * Distance.MetersPerKilometer,
            entry.Position.Y * Distance.MetersPerKilometer,
            entry.Position.Z * Distance.MetersPerKilometer);
        var velMs = new Vector3D(
            entry.Velocity.X * Distance.MetersPerKilometer,
            entry.Velocity.Y * Distance.MetersPerKilometer,
            entry.Velocity.Z * Distance.MetersPerKilometer);

        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (posM, velMs),
            NavigationResultStatus.ValidatedReference,
            epoch,
            targetFrame,
            DataSource);
    }

    private JplEphemerisEntry? FindExactEpoch(List<JplEphemerisEntry> entries, double jd)
    {
        var idx = BinarySearch(entries, jd);
        if (idx >= 0)
            return entries[idx];

        var insert = ~idx;
        if (insert < entries.Count && Math.Abs(entries[insert].Jd - jd) <= _epochToleranceDays)
            return entries[insert];

        if (insert > 0 && Math.Abs(entries[insert - 1].Jd - jd) <= _epochToleranceDays)
            return entries[insert - 1];

        return null;
    }

    private static int BinarySearch(List<JplEphemerisEntry> entries, double jd)
    {
        int lo = 0, hi = entries.Count - 1;
        while (lo <= hi)
        {
            var mid = (lo + hi) / 2;
            var cmp = entries[mid].Jd.CompareTo(jd);
            if (cmp == 0) return mid;
            if (cmp < 0) lo = mid + 1;
            else hi = mid - 1;
        }
        return ~lo;
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

        var relPos = targetPos - observerPos;
        var relVel = targetVel - observerVel;

        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (relPos, relVel),
            NavigationResultStatus.ValidatedReference,
            epoch,
            ReferenceFrame.Barycentric,
            DataSource);
    }

    public bool IsBodySupported(string bodyId) => _entriesByBody.ContainsKey(bodyId);

    public (AstroTime Start, AstroTime End)? GetCoverage(string bodyId) =>
        _entriesByBody.ContainsKey(bodyId) ? (_coverageStart, _coverageEnd) : null;

    public List<string> SupportedBodies => _entriesByBody.Keys.ToList();

    public AstroTime CoverageStart => _coverageStart;
    public AstroTime CoverageEnd => _coverageEnd;
}
