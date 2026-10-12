using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Astronomy.Time;

/// <summary>
/// Loads and authenticates the SN5 offline time-data asset
/// (<c>leap_seconds.sn5json</c>, schema SN-TIME-01) derived from the official
/// NAIF leapseconds kernel. Integrity model (same as SN4): SHA-256 over the
/// distributed bytes verified against the trusted digest embedded below —
/// never against a sidecar hash. Time-data updates are explicit and auditable:
/// regenerate the asset with <c>tools/sn5_extract_leapseconds.py</c> and
/// update <see cref="ExpectedAssetSha256"/> in the same commit.
/// No network, CSPICE, or OS leap-second data is consulted at runtime.
/// </summary>
public sealed class LeapSecondProvider
{
    public const string SupportedSchema = "SN-TIME-01";

    /// <summary>Trusted SHA-256 of the distributed asset bytes (source-anchored).</summary>
    public const string ExpectedAssetSha256 =
        "022d49a4d351a032dfe6f727c2c121cb72177a9a29a0a8da4fdf8b6985344003";

    public LeapSecondTable Table { get; }
    public string KernelFilename { get; }
    public string KernelSourceUrl { get; }
    public string KernelSha256 { get; }
    public string RetrievalDate { get; }
    public string LastAnnouncedLeapSecondUtc { get; }
    public string AssetSha256 { get; }
    public long AssetBytes { get; }

    /// <summary>Short kernel identity for console/provenance display.</summary>
    public string Identity =>
        $"{KernelFilename} (SHA-256 {KernelSha256[..16]}...)";

    private LeapSecondProvider(
        LeapSecondTable table, string kernelFilename, string kernelSourceUrl,
        string kernelSha256, string retrievalDate, string lastLeap,
        string assetSha256, long assetBytes)
    {
        Table = table;
        KernelFilename = kernelFilename;
        KernelSourceUrl = kernelSourceUrl;
        KernelSha256 = kernelSha256;
        RetrievalDate = retrievalDate;
        LastAnnouncedLeapSecondUtc = lastLeap;
        AssetSha256 = assetSha256;
        AssetBytes = assetBytes;
    }

    public static LeapSecondProvider Load(string path)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(path);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            throw new InvalidDataException($"Time-data asset not found or unreadable: {path}", ex);
        }

        string actual = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        if (!actual.Equals(ExpectedAssetSha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException(
                $"Time-data asset integrity failure: SHA-256 {actual} != trusted {ExpectedAssetSha256}.");

        JsonDocument doc;
        try
        {
            doc = JsonDocument.Parse(bytes);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException("Time-data asset is malformed JSON.", ex);
        }

        using (doc)
        {
            var root = doc.RootElement;
            string schema = GetString(root, "schema");
            if (schema != SupportedSchema)
                throw new InvalidDataException(
                    $"Unsupported time-data schema '{schema}' (supported: {SupportedSchema}).");

            if (!root.TryGetProperty("kernel", out var kernel))
                throw new InvalidDataException("Time-data asset missing provenance 'kernel'.");
            string filename = GetString(kernel, "filename");
            string url = GetString(kernel, "sourceUrl");
            string ksha = GetString(kernel, "sha256");
            if (string.IsNullOrWhiteSpace(filename) || string.IsNullOrWhiteSpace(url) ||
                ksha.Length != 64 || !ksha.All(Uri.IsHexDigit))
                throw new InvalidDataException("Time-data asset carries invalid kernel provenance.");
            string retrieval = GetString(root, "retrievalDate");
            string lastLeap = GetString(root, "lastAnnouncedLeapSecondUtc");
            if (!UtcInstant.TryParse(lastLeap, out var leap, out _) || !leap.IsLeapSecond)
                throw new InvalidDataException("Time-data asset carries an invalid last-announced leap second.");

            if (!root.TryGetProperty("deltet", out var deltet))
                throw new InvalidDataException("Time-data asset missing 'deltet' constants.");
            double ta = GetDouble(deltet, "deltaTaSec");
            double k = GetDouble(deltet, "k");
            double eb = GetDouble(deltet, "eb");
            double m0 = GetDouble(deltet, "m0Rad");
            double m1 = GetDouble(deltet, "m1RadPerSec");

            if (!root.TryGetProperty("transitions", out var tr) || tr.ValueKind != JsonValueKind.Array)
                throw new InvalidDataException("Time-data asset missing 'transitions' table.");
            var entries = new List<(string UtcMidnight, int Offset)>();
            foreach (var item in tr.EnumerateArray())
            {
                string utc = GetString(item, "utc");
                if (!item.TryGetProperty("offset", out var off) || off.ValueKind != JsonValueKind.Number ||
                    !off.TryGetInt32(out int offset))
                    throw new InvalidDataException("Transition 'offset' must be an integer.");
                entries.Add((utc, offset));
            }

            var table = LeapSecondTable.Create(entries, ta, k, eb, m0, m1);
            return new LeapSecondProvider(table, filename, url, ksha.ToLowerInvariant(),
                retrieval, lastLeap, actual, bytes.Length);
        }
    }

    /// <summary>Default runtime location (App Data directory).</summary>
    public static string DefaultAssetPath() =>
        Path.Combine(AppContext.BaseDirectory, "Data", "leap_seconds.sn5json");

    private static string GetString(JsonElement el, string name)
    {
        if (!el.TryGetProperty(name, out var v) || v.ValueKind != JsonValueKind.String)
            throw new InvalidDataException($"Time-data asset missing string '{name}'.");
        return v.GetString() ?? throw new InvalidDataException($"Time-data asset missing string '{name}'.");
    }

    private static double GetDouble(JsonElement el, string name)
    {
        if (!el.TryGetProperty(name, out var v) || v.ValueKind != JsonValueKind.Number ||
            !v.TryGetDouble(out double d) || double.IsNaN(d) || double.IsInfinity(d))
            throw new InvalidDataException($"Time-data asset missing finite number '{name}'.");
        return d;
    }
}
