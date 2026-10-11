using System.Text;
using System.Text.Json;

namespace Astronomy.Ephemeris;

/// <summary>
/// Indexed metadata for one SPK Type 2 segment inside an SNEPH binary
/// dataset. Carries everything needed to locate and evaluate records
/// WITHOUT materializing coefficients: file offset, record length, time
/// base, and center-chain relationships.
/// </summary>
public sealed class IndexedSegment
{
    public required string BodyId { get; init; }
    public required string CenterBodyId { get; init; }
    public required int TargetId { get; init; }
    public required int CenterId { get; init; }
    public required int FrameId { get; init; }
    public required int SegmentType { get; init; }
    public required int Degree { get; init; }
    public required double RecordIntervalSeconds { get; init; }
    public required double InitEt { get; init; }
    public required long RecordCount { get; init; }
    public required double CoverageStartJd { get; init; }
    public required double CoverageEndJd { get; init; }
    public required long DataOffset { get; init; }
    public required int RecordLength { get; init; }

    /// <summary>Bytes per record (RecordLength binary64 words).</summary>
    public long RecordByteLength => (long)RecordLength * 8L;

    /// <summary>End of coverage in ET seconds past J2000.</summary>
    public double EndEt => InitEt + (double)RecordCount * RecordIntervalSeconds;
}

/// <summary>
/// Provenance subset parsed from the dataset's embedded JSON block.
/// The full JSON text is preserved verbatim on <see cref="IndexedDataset"/>.
/// </summary>
public sealed class Sn4Provenance
{
    public string Source { get; init; } = "";
    public string SourceUrl { get; init; } = "";
    public string RetrievalDate { get; init; } = "";
    public string SpkMd5 { get; init; } = "";
    public string SpkSha256 { get; init; } = "";
    public string Frame { get; init; } = "";
    public string Origin { get; init; } = "";
    public string TimeScale { get; init; } = "";
    public string Note { get; init; } = "";
}

/// <summary>
/// Index-only view of an SNEPH binary dataset: provenance plus the segment
/// table. Coefficients stay on disk until individually requested.
/// </summary>
public sealed class IndexedDataset
{
    public required string ProvenanceJson { get; init; }
    public required Sn4Provenance Provenance { get; init; }
    public required List<IndexedSegment> Segments { get; init; }
    public required long TotalRecords { get; init; }
}

/// <summary>
/// Reader for the versioned SNEPH indexed binary coefficient format
/// (magic "SNEPH04B", version 1; full layout documented in
/// tools/sn4_extract_chebyshev.py and the SN4 coverage document).
///
/// The reader loads provenance + segment index only; coefficient records
/// are fetched on demand via <see cref="ReadRecord"/>. Every offset and
/// count is bounds-checked with overflow-checked arithmetic before any
/// file access, so truncated or malformed files fail with
/// <see cref="InvalidDataException"/> instead of silent misreads.
/// Byte order is little-endian; files with a bad magic are rejected
/// (no silent byte-swapped reinterpretation).
/// </summary>
public static class ChebyshevBinaryFormat
{
    public static readonly byte[] Magic = Encoding.ASCII.GetBytes("SNEPH04B");
    public const int SupportedVersion = 1;

    private const int MaxSegments = 1024;
    private const int MaxProvenanceBytes = 1 << 20;
    private const int MaxIdBytes = 128;
    private const int MaxDegree = 50;

    /// <summary>
    /// Reads and fully validates the dataset header + segment index.
    /// No coefficient data is materialized.
    /// </summary>
    public static IndexedDataset ReadIndex(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        long fileLength = stream.Length;
        using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);

        byte[] magic;
        try
        {
            magic = reader.ReadBytes(Magic.Length);
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException("SNEPH dataset is truncated in the header.", ex);
        }
        if (magic.Length != Magic.Length || !magic.SequenceEqual(Magic))
            throw new InvalidDataException(
                "SNEPH dataset has an unrecognized magic (not an SNEPH indexed coefficient file).");

        int version = ReadInt32(reader, "version");
        if (version != SupportedVersion)
            throw new InvalidDataException(
                $"SNEPH dataset version {version} is not supported (supported: {SupportedVersion}).");

        int segmentCount = ReadInt32(reader, "segmentCount");
        if (segmentCount <= 0 || segmentCount > MaxSegments)
            throw new InvalidDataException(
                $"SNEPH dataset has invalid segment count {segmentCount}.");

        long totalRecords = ReadInt64(reader, "totalRecords");
        if (totalRecords <= 0)
            throw new InvalidDataException(
                $"SNEPH dataset has invalid total record count {totalRecords}.");

        int provenanceLength = ReadInt32(reader, "provenanceLength");
        if (provenanceLength < 2 || provenanceLength > MaxProvenanceBytes)
            throw new InvalidDataException(
                $"SNEPH dataset has invalid provenance length {provenanceLength}.");

        byte[] provenanceBytes;
        try
        {
            provenanceBytes = reader.ReadBytes(provenanceLength);
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException("SNEPH dataset is truncated in the provenance block.", ex);
        }
        if (provenanceBytes.Length != provenanceLength)
            throw new InvalidDataException("SNEPH dataset is truncated in the provenance block.");
        string provenanceJson = Encoding.UTF8.GetString(provenanceBytes);
        var provenance = ParseProvenance(provenanceJson);

        var segments = new List<IndexedSegment>(segmentCount);
        var bodyIds = new HashSet<string>(StringComparer.Ordinal);
        for (int i = 0; i < segmentCount; i++)
            segments.Add(ReadSegment(reader, i));

        foreach (var seg in segments)
        {
            if (!bodyIds.Add(seg.BodyId))
                throw new InvalidDataException(
                    $"SNEPH dataset contains a duplicate body id '{seg.BodyId}'.");
        }

        long summedRecords = 0;
        foreach (var seg in segments)
            summedRecords = checked(summedRecords + seg.RecordCount);
        if (summedRecords != totalRecords)
            throw new InvalidDataException(
                $"SNEPH dataset record total {summedRecords} does not match header {totalRecords}.");

        // Data regions must lie inside the file and must not overlap.
        var ordered = segments.OrderBy(s => s.DataOffset).ToList();
        foreach (var seg in ordered)
        {
            if (seg.DataOffset < 0)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' has a negative data offset.");
            long span;
            try
            {
                span = checked(seg.RecordCount * seg.RecordByteLength);
            }
            catch (OverflowException ex)
            {
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' record span overflows.", ex);
            }
            long end;
            try
            {
                end = checked(seg.DataOffset + span);
            }
            catch (OverflowException ex)
            {
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' data range overflows.", ex);
            }
            if (end > fileLength)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' data range [{seg.DataOffset}, {end}) exceeds file length {fileLength} (truncated file).");
        }
        for (int i = 1; i < ordered.Count; i++)
        {
            long prevEnd = checked(ordered[i - 1].DataOffset +
                checked(ordered[i - 1].RecordCount * ordered[i - 1].RecordByteLength));
            if (ordered[i].DataOffset < prevEnd)
                throw new InvalidDataException(
                    $"Segments '{ordered[i - 1].BodyId}' and '{ordered[i].BodyId}' have overlapping data ranges.");
        }

        return new IndexedDataset
        {
            ProvenanceJson = provenanceJson,
            Provenance = provenance,
            Segments = segments,
            TotalRecords = totalRecords,
        };
    }

    /// <summary>
    /// Reads one coefficient record (verbatim binary64 words) by index.
    /// The returned array is a fresh copy owned by the caller.
    /// </summary>
    public static double[] ReadRecord(Stream stream, IndexedSegment seg, long index)
    {
        if (index < 0 || index >= seg.RecordCount)
            throw new ArgumentOutOfRangeException(nameof(index));
        long offset;
        try
        {
            offset = checked(seg.DataOffset + checked(index * seg.RecordByteLength));
        }
        catch (OverflowException ex)
        {
            throw new InvalidDataException(
                $"Segment '{seg.BodyId}' record {index} offset overflows.", ex);
        }
        if (!stream.CanSeek)
            throw new InvalidDataException("SNEPH dataset stream is not seekable.");
        if (offset < 0 || offset + seg.RecordByteLength > stream.Length)
            throw new InvalidDataException(
                $"Segment '{seg.BodyId}' record {index} lies outside the file (truncated file).");

        stream.Seek(offset, SeekOrigin.Begin);
        byte[] bytes = new byte[seg.RecordByteLength];
        int read = 0;
        while (read < bytes.Length)
        {
            int n = stream.Read(bytes, read, bytes.Length - read);
            if (n == 0)
                throw new InvalidDataException(
                    $"Segment '{seg.BodyId}' record {index} is truncated.");
            read += n;
        }
        if (!BitConverter.IsLittleEndian)
            throw new InvalidDataException(
                "SNEPH datasets are little-endian; big-endian hosts are not supported.");
        var record = new double[seg.RecordLength];
        Buffer.BlockCopy(bytes, 0, record, 0, bytes.Length);
        return record;
    }

    private static IndexedSegment ReadSegment(BinaryReader reader, int ordinal)
    {
        string bodyId = ReadId(reader, $"segment {ordinal} bodyId");
        string centerBodyId = ReadId(reader, $"segment {ordinal} centerBodyId");
        int targetId = ReadInt32(reader, "targetId");
        int centerId = ReadInt32(reader, "centerId");
        int frameId = ReadInt32(reader, "frameId");
        int segmentType = ReadInt32(reader, "segmentType");
        int degree = ReadInt32(reader, "degree");
        double interval = ReadDouble(reader, "recordIntervalSeconds");
        double initEt = ReadDouble(reader, "initEt");
        long recordCount = ReadInt64(reader, "recordCount");
        double coverageStartJd = ReadDouble(reader, "coverageStartJd");
        double coverageEndJd = ReadDouble(reader, "coverageEndJd");
        long dataOffset = ReadInt64(reader, "dataOffset");
        long recordLength = ReadInt64(reader, "recordLength");

        if (string.IsNullOrWhiteSpace(bodyId))
            throw new InvalidDataException($"Segment {ordinal} is missing its body id.");
        if (string.IsNullOrWhiteSpace(centerBodyId))
            throw new InvalidDataException($"Segment '{bodyId}' is missing its center body id.");
        if (segmentType != 2)
            throw new InvalidDataException(
                $"Segment '{bodyId}' has unsupported SPK type {segmentType} (only Type 2 supported).");
        if (frameId != 1)
            throw new InvalidDataException(
                $"Segment '{bodyId}' has unsupported frame id {frameId} (only J2000/frame 1 supported).");
        if (degree < 0 || degree > MaxDegree)
            throw new InvalidDataException($"Segment '{bodyId}' has invalid degree {degree}.");
        if (!double.IsFinite(interval) || interval <= 0.0)
            throw new InvalidDataException($"Segment '{bodyId}' has invalid record interval.");
        if (!double.IsFinite(initEt))
            throw new InvalidDataException($"Segment '{bodyId}' has non-finite INIT epoch.");
        if (recordCount <= 0)
            throw new InvalidDataException($"Segment '{bodyId}' has invalid record count {recordCount}.");
        if (!double.IsFinite(coverageStartJd) || !double.IsFinite(coverageEndJd) ||
            coverageEndJd <= coverageStartJd)
            throw new InvalidDataException($"Segment '{bodyId}' has invalid coverage interval.");
        long expectedLength = 2L + 3L * ((long)degree + 1L);
        if (recordLength != expectedLength)
            throw new InvalidDataException(
                $"Segment '{bodyId}' record length {recordLength} does not match degree {degree} (expected {expectedLength}).");
        if (dataOffset < 0)
            throw new InvalidDataException($"Segment '{bodyId}' has a negative data offset.");

        return new IndexedSegment
        {
            BodyId = bodyId,
            CenterBodyId = centerBodyId,
            TargetId = targetId,
            CenterId = centerId,
            FrameId = frameId,
            SegmentType = segmentType,
            Degree = degree,
            RecordIntervalSeconds = interval,
            InitEt = initEt,
            RecordCount = recordCount,
            CoverageStartJd = coverageStartJd,
            CoverageEndJd = coverageEndJd,
            DataOffset = dataOffset,
            RecordLength = (int)recordLength,
        };
    }

    private static string ReadId(BinaryReader reader, string what)
    {
        int length = ReadInt32(reader, what);
        if (length <= 0 || length > MaxIdBytes)
            throw new InvalidDataException($"SNEPH dataset has an invalid identifier length for {what}.");
        byte[] bytes;
        try
        {
            bytes = reader.ReadBytes(length);
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException($"SNEPH dataset is truncated while reading {what}.", ex);
        }
        if (bytes.Length != length)
            throw new InvalidDataException($"SNEPH dataset is truncated while reading {what}.");
        return Encoding.UTF8.GetString(bytes);
    }

    private static int ReadInt32(BinaryReader reader, string what)
    {
        try
        {
            return reader.ReadInt32();
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException($"SNEPH dataset is truncated while reading {what}.", ex);
        }
    }

    private static long ReadInt64(BinaryReader reader, string what)
    {
        try
        {
            return reader.ReadInt64();
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException($"SNEPH dataset is truncated while reading {what}.", ex);
        }
    }

    private static double ReadDouble(BinaryReader reader, string what)
    {
        try
        {
            return reader.ReadDouble();
        }
        catch (EndOfStreamException ex)
        {
            throw new InvalidDataException($"SNEPH dataset is truncated while reading {what}.", ex);
        }
    }

    private static Sn4Provenance ParseProvenance(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            return new Sn4Provenance
            {
                Source = GetString(root, "source"),
                SourceUrl = GetString(root, "sourceUrl"),
                RetrievalDate = GetString(root, "retrievalDate"),
                SpkMd5 = GetString(root, "spkMd5"),
                SpkSha256 = GetString(root, "spkSha256"),
                Frame = GetString(root, "frame"),
                Origin = GetString(root, "origin"),
                TimeScale = GetString(root, "timeScale"),
                Note = GetString(root, "note"),
            };
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"SNEPH dataset has an invalid provenance block: {ex.Message}", ex);
        }
    }

    private static string GetString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var el) && el.ValueKind == JsonValueKind.String
            ? el.GetString() ?? ""
            : "";
}
