using System.Text.Json.Serialization;

namespace Astronomy.Ephemeris;

/// <summary>
/// Runtime dataset of genuine SPK Type 2 (Chebyshev position-only) records
/// extracted verbatim from JPL DE440s. Coefficients are stored in km;
/// the managed runtime converts to SI (m, m/s) once, on output.
/// </summary>
public sealed class ChebyshevDatasetMetadata
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

    [JsonPropertyName("frame")]
    public string Frame { get; set; } = "";

    [JsonPropertyName("origin")]
    public string Origin { get; set; } = "";

    [JsonPropertyName("timeScale")]
    public string TimeScale { get; set; } = "";

    [JsonPropertyName("units")]
    public ChebyshevDatasetUnits Units { get; set; } = new();

    [JsonPropertyName("aberrationCorrection")]
    public string AberrationCorrection { get; set; } = "";

    [JsonPropertyName("segmentType")]
    public int SegmentType { get; set; }

    [JsonPropertyName("totalRecords")]
    public int TotalRecords { get; set; }

    [JsonPropertyName("note")]
    public string Note { get; set; } = "";
}

public sealed class ChebyshevDatasetUnits
{
    [JsonPropertyName("position")]
    public string Position { get; set; } = "";

    [JsonPropertyName("velocity")]
    public string Velocity { get; set; } = "";
}

public sealed class ChebyshevSegmentData
{
    [JsonPropertyName("bodyId")]
    public string BodyId { get; set; } = "";

    [JsonPropertyName("targetId")]
    public int TargetId { get; set; }

    [JsonPropertyName("centerId")]
    public int CenterId { get; set; }

    [JsonPropertyName("centerBodyId")]
    public string CenterBodyId { get; set; } = "";

    [JsonPropertyName("frameId")]
    public int FrameId { get; set; }

    [JsonPropertyName("frame")]
    public string Frame { get; set; } = "";

    [JsonPropertyName("segmentType")]
    public int SegmentType { get; set; }

    [JsonPropertyName("degree")]
    public int Degree { get; set; }

    [JsonPropertyName("recordIntervalSeconds")]
    public double RecordIntervalSeconds { get; set; }

    [JsonPropertyName("initEt")]
    public double InitEt { get; set; }

    [JsonPropertyName("recordCount")]
    public int RecordCount { get; set; }

    [JsonPropertyName("coverageStartJd")]
    public double CoverageStartJd { get; set; }

    [JsonPropertyName("coverageEndJd")]
    public double CoverageEndJd { get; set; }

    [JsonPropertyName("records")]
    public List<double[]> Records { get; set; } = new();
}

public sealed class ChebyshevDataset
{
    [JsonPropertyName("metadata")]
    public ChebyshevDatasetMetadata Metadata { get; set; } = new();

    [JsonPropertyName("segments")]
    public List<ChebyshevSegmentData> Segments { get; set; } = new();
}
