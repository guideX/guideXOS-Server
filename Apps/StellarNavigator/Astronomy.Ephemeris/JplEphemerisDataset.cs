using System.Text.Json.Serialization;

namespace Astronomy.Ephemeris;

public sealed class JplEphemerisMetadata
{
    [JsonPropertyName("source")]
    public string Source { get; set; } = "";

    [JsonPropertyName("sourceUrl")]
    public string SourceUrl { get; set; } = "";

    [JsonPropertyName("retrievalDate")]
    public string RetrievalDate { get; set; } = "";

    [JsonPropertyName("spkMd5")]
    public string SpkMd5 { get; set; } = "";

    [JsonPropertyName("spkSha256")]
    public string SpkSha256 { get; set; } = "";

    [JsonPropertyName("outputSha256")]
    public string OutputSha256 { get; set; } = "";

    [JsonPropertyName("coverage")]
    public JplEphemerisCoverage Coverage { get; set; } = new();

    [JsonPropertyName("frame")]
    public string Frame { get; set; } = "";

    [JsonPropertyName("origin")]
    public string Origin { get; set; } = "";

    [JsonPropertyName("timeScale")]
    public string TimeScale { get; set; } = "";

    [JsonPropertyName("units")]
    public JplEphemerisUnits Units { get; set; } = new();

    [JsonPropertyName("aberrationCorrection")]
    public string AberrationCorrection { get; set; } = "";

    [JsonPropertyName("bodies")]
    public Dictionary<string, JplEphemerisBodyInfo> Bodies { get; set; } = new();

    [JsonPropertyName("sampleIntervalDays")]
    public double SampleIntervalDays { get; set; }

    [JsonPropertyName("totalEntries")]
    public int TotalEntries { get; set; }

    [JsonPropertyName("note")]
    public string Note { get; set; } = "";
}

public sealed class JplEphemerisCoverage
{
    [JsonPropertyName("start")]
    public string Start { get; set; } = "";

    [JsonPropertyName("end")]
    public string End { get; set; } = "";
}

public sealed class JplEphemerisUnits
{
    [JsonPropertyName("position")]
    public string Position { get; set; } = "";

    [JsonPropertyName("velocity")]
    public string Velocity { get; set; } = "";
}

public sealed class JplEphemerisBodyInfo
{
    [JsonPropertyName("naifId")]
    public int NaifId { get; set; }

    [JsonPropertyName("description")]
    public string Description { get; set; } = "";
}

public sealed class JplEphemerisData
{
    [JsonPropertyName("metadata")]
    public JplEphemerisMetadata Metadata { get; set; } = new();

    [JsonPropertyName("entries")]
    public List<JplEphemerisEntry> Entries { get; set; } = new();
}

public sealed class JplEphemerisEntry
{
    [JsonPropertyName("bodyId")]
    public string BodyId { get; set; } = "";

    [JsonPropertyName("naifId")]
    public int NaifId { get; set; }

    [JsonPropertyName("jd")]
    public double Jd { get; set; }

    [JsonPropertyName("et")]
    public double Et { get; set; }

    [JsonPropertyName("position")]
    public JplEphemerisVector Position { get; set; } = new();

    [JsonPropertyName("velocity")]
    public JplEphemerisVector Velocity { get; set; } = new();
}

public sealed class JplEphemerisVector
{
    [JsonPropertyName("x")]
    public double X { get; set; }

    [JsonPropertyName("y")]
    public double Y { get; set; }

    [JsonPropertyName("z")]
    public double Z { get; set; }
}
