using System.Text.Json;
using Astronomy.Core;
using Navigation.Core;

namespace Astronomy.Ephemeris;

public sealed class PrecomputedEphemerisProvider : IEphemerisProvider
{
    private readonly Dictionary<string, EphemerisEntry> _entries = new(StringComparer.Ordinal);
    private readonly AstroTime _coverageStart;
    private readonly AstroTime _coverageEnd;

    public string DataSource { get; }
    public ReferenceFrame NativeFrame { get; }

    public PrecomputedEphemerisProvider(string jsonPath)
    {
        var json = File.ReadAllText(jsonPath);
        Load(json);
        DataSource = "Synthetic precomputed demonstration data (SN1)";
        NativeFrame = ReferenceFrame.Heliocentric;
        _coverageStart = AstroTime.FromJ2000Days(-365.25);
        _coverageEnd = AstroTime.FromJ2000Days(365.25);
    }

    private void Load(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (!root.TryGetProperty("entries", out var entriesEl) || entriesEl.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Ephemeris JSON must contain an 'entries' array");

        foreach (var entryEl in entriesEl.EnumerateArray())
        {
            var bodyId = entryEl.GetProperty("bodyId").GetString() ?? throw new InvalidDataException("Entry missing bodyId");
            var jd = entryEl.GetProperty("jd").GetDouble();
            var epoch = AstroTime.FromJdUtc(jd);

            var posEl = entryEl.GetProperty("position");
            var velEl = entryEl.GetProperty("velocity");

            var posAu = new Vector3D(
                posEl.GetProperty("x").GetDouble(),
                posEl.GetProperty("y").GetDouble(),
                posEl.GetProperty("z").GetDouble());
            var velAuDay = new Vector3D(
                velEl.GetProperty("x").GetDouble(),
                velEl.GetProperty("y").GetDouble(),
                velEl.GetProperty("z").GetDouble());

            var posM = posAu * Distance.MetersPerAstronomicalUnit;
            var velMs = velAuDay * Distance.MetersPerAstronomicalUnit / Time.SecondsPerDay;

            _entries[bodyId] = new EphemerisEntry { Epoch = epoch, Position = posM, Velocity = velMs };
        }
    }

    public NavigationResult<(Vector3D Position, Vector3D Velocity)> GetState(
        string bodyId, AstroTime epoch, ReferenceFrame targetFrame)
    {
        if (!_entries.TryGetValue(bodyId, out var entry))
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.InsufficientInformation,
                $"No ephemeris data for body '{bodyId}'");

        if (epoch.JdUtc < _coverageStart.JdUtc || epoch.JdUtc > _coverageEnd.JdUtc)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.StaleData,
                $"Epoch {epoch} is outside coverage range [{_coverageStart}, {_coverageEnd}]");

        if (targetFrame.Kind != ReferenceFrameKind.Heliocentric)
            return NavigationResult<(Vector3D, Vector3D)>.Failure(
                NavigationResultStatus.UnsupportedCalculation,
                $"Frame conversion to {targetFrame} is not supported in SN1");

        return NavigationResult<(Vector3D, Vector3D)>.Success(
            (entry.Position, entry.Velocity),
            NavigationResultStatus.ApproximateOrSimulated,
            epoch,
            targetFrame,
            DataSource);
    }

    public bool IsBodySupported(string bodyId) => _entries.ContainsKey(bodyId);

    public (AstroTime Start, AstroTime End)? GetCoverage(string bodyId) =>
        _entries.ContainsKey(bodyId) ? (_coverageStart, _coverageEnd) : null;

    private sealed class EphemerisEntry
    {
        public required AstroTime Epoch { get; init; }
        public required Vector3D Position { get; init; }
        public required Vector3D Velocity { get; init; }
    }
}
