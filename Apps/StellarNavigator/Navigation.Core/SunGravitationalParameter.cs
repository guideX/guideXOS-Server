using System.Security.Cryptography;
using System.Text.Json;

namespace Navigation.Core;

/// <summary>Authenticated solar GM derived from the NAIF DE440 text PCK.</summary>
public sealed class SunGravitationalParameter
{
    public const string RuntimeAssetName = "sun_gm_de440.sn8json";
    public const string RuntimeAssetIdentity = "SN8-SUN-GM-DE440-v1";
    public const string SourceFilename = "gm_de440.tpc";
    public const string SourceUrl = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/gm_de440.tpc";
    public const string SourceSha256 = "924ddf4fb9ead9fe8a1aa55780bcabde40b09d00065d58226e24b68d8092f140";
    public const double SourceValueKm3PerSecond2 = 1.3271244004127942e11;
    public const double ValueMeters3PerSecond2 = SourceValueKm3PerSecond2 * 1_000_000_000.0;

    // This digest authenticates the compact runtime asset. The source PCK digest
    // above authenticates the scientific value's provenance.
    public const string RuntimeAssetSha256 = "a93a6671d32f248ef7c05a749c1e9ac52b2462919f9e9a07ef01bcf93229808b";

    private SunGravitationalParameter() { }

    public double MuMeters3PerSecond2 => ValueMeters3PerSecond2;
    public string Identity => RuntimeAssetIdentity;
    public string Provenance =>
        $"{RuntimeAssetIdentity}; {SourceFilename} ({SourceUrl}), SHA-256 {SourceSha256}; " +
        $"BODY10_GM={SourceValueKm3PerSecond2:R} km^3/s^2 converted by 1e9 to m^3/s^2";

    public static SunGravitationalParameter Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        var info = new FileInfo(path);
        if (!info.Exists)
            throw new FileNotFoundException("Authenticated solar GM runtime asset was not found.", path);
        if (info.Length is <= 0 or > 4096)
            throw new InvalidDataException("Solar GM runtime asset must be between 1 and 4096 bytes.");
        byte[] bytes = File.ReadAllBytes(path);
        string digest = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        if (!string.Equals(digest, RuntimeAssetSha256, StringComparison.Ordinal))
            throw new InvalidDataException($"Solar GM runtime asset checksum mismatch: expected {RuntimeAssetSha256}, got {digest}.");

        using JsonDocument document = JsonDocument.Parse(bytes);
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object ||
            ReadString(root, "identity") != RuntimeAssetIdentity ||
            ReadString(root, "sourceFilename") != SourceFilename ||
            ReadString(root, "sourceUrl") != SourceUrl ||
            ReadString(root, "sourceSha256") != SourceSha256 ||
            ReadString(root, "centralBody") != "Sun" ||
            !root.TryGetProperty("naifId", out var naif) || naif.GetInt32() != 10 ||
            !root.TryGetProperty("gmKm3PerSecond2", out var gm) || !gm.TryGetDouble(out double value) ||
            value != SourceValueKm3PerSecond2)
            throw new InvalidDataException("Solar GM runtime asset metadata does not match the authenticated DE440 Sun parameter.");

        return new SunGravitationalParameter();
    }

    private static string? ReadString(JsonElement element, string property) =>
        element.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;
}
