using System.Text;
using Astronomy.Ephemeris;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SNEPH binary format integrity: index loading, bounds validation, and
/// rejection of malformed inputs. Uses small hand-built files plus
/// corrupted copies of the real dataset (with checksum verification
/// disabled via expectedSha256: null where the bytes are intentionally
/// altered).
/// </summary>
public class BinaryFormatTests : IDisposable
{
    private readonly string _datasetPath;
    private readonly List<string> _tempFiles = new();

    public BinaryFormatTests()
    {
        _datasetPath = Sn4Paths.DatasetPath();
    }

    public void Dispose()
    {
        foreach (var f in _tempFiles)
        {
            try { if (File.Exists(f)) File.Delete(f); } catch { }
        }
    }

    private string TempFile() =>
        Path.Combine(Path.GetTempPath(), "sn4fmt_" + Guid.NewGuid().ToString("N") + ".sn4bin");

    private string Track(string path)
    {
        _tempFiles.Add(path);
        return path;
    }

    [Fact]
    public void RealDataset_IndexLoads_WithoutMaterializingCoefficients()
    {
        var dataset = ChebyshevBinaryFormat.ReadIndex(_datasetPath);
        Assert.Equal(13, dataset.Segments.Count);
        Assert.Equal(106177L, dataset.TotalRecords);
        Assert.Contains("DE440s", dataset.Provenance.Source);
        Assert.Equal("3917ee56769db332790c751e2168843d", dataset.Provenance.SpkMd5);
        Assert.Equal(
            "c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2",
            dataset.Provenance.SpkSha256);

        // Every new planet present with its genuine descriptor values.
        var byBody = dataset.Segments.ToDictionary(s => s.BodyId);
        Assert.Equal(1, byBody["mercury"].TargetId);
        Assert.Equal(0, byBody["mercury"].CenterId);
        Assert.Equal(13, byBody["mercury"].Degree);
        Assert.Equal(13700L, byBody["mercury"].RecordCount);
        Assert.Equal(2, byBody["venus"].TargetId);
        Assert.Equal(6, byBody["saturn"].TargetId);
        Assert.Equal(7, byBody["uranus"].TargetId);
        Assert.Equal(8, byBody["neptune"].TargetId);
        // Body-center segments keep their true centers (NOT relabeled to SSB).
        Assert.Equal(199, byBody["mercury_center"].TargetId);
        Assert.Equal(1, byBody["mercury_center"].CenterId);
        Assert.Equal(299, byBody["venus_center"].TargetId);
        Assert.Equal(2, byBody["venus_center"].CenterId);
        // Pluto stays out of scope.
        Assert.False(byBody.ContainsKey("pluto"));
    }

    [Fact]
    public void RealDataset_AllSegmentOffsetsWithinFile()
    {
        long length = new FileInfo(_datasetPath).Length;
        var dataset = ChebyshevBinaryFormat.ReadIndex(_datasetPath);
        foreach (var seg in dataset.Segments)
        {
            long end = checked(seg.DataOffset + checked(seg.RecordCount * seg.RecordByteLength));
            Assert.True(end <= length, $"{seg.BodyId} exceeds file length");
        }
    }

    [Fact]
    public void ReadRecord_RandomAccess_ReturnsVerbatimLayout()
    {
        var dataset = ChebyshevBinaryFormat.ReadIndex(_datasetPath);
        var mercury = dataset.Segments.First(s => s.BodyId == "mercury");
        Assert.Equal(44, mercury.RecordLength);
        using var stream = File.OpenRead(_datasetPath);
        var first = ChebyshevBinaryFormat.ReadRecord(stream, mercury, 0);
        var last = ChebyshevBinaryFormat.ReadRecord(stream, mercury, mercury.RecordCount - 1);
        Assert.Equal(44, first.Length);
        // MID/RADIUS geometry: first record midpoint = INIT + INTLEN/2.
        Assert.Equal(mercury.InitEt + mercury.RecordIntervalSeconds / 2.0, first[0], 9);
        Assert.Equal(mercury.RecordIntervalSeconds / 2.0, first[1], 12);
        Assert.True(last[0] > first[0]);
    }

    [Fact]
    public void ReadRecord_OutOfRange_Throws()
    {
        var dataset = ChebyshevBinaryFormat.ReadIndex(_datasetPath);
        var saturn = dataset.Segments.First(s => s.BodyId == "saturn");
        using var stream = File.OpenRead(_datasetPath);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ChebyshevBinaryFormat.ReadRecord(stream, saturn, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ChebyshevBinaryFormat.ReadRecord(stream, saturn, saturn.RecordCount));
    }

    [Fact]
    public void MinimalFile_RoundTrips()
    {
        string path = Track(TempFile());
        WriteMinimalFile(path, version: 1, recordCount: 3);
        var dataset = ChebyshevBinaryFormat.ReadIndex(path);
        Assert.Single(dataset.Segments);
        Assert.Equal(3L, dataset.TotalRecords);
        var seg = dataset.Segments[0];
        Assert.Equal("tiny", seg.BodyId);
        Assert.Equal("ssb", seg.CenterBodyId);
        using var stream = File.OpenRead(path);
        var rec = ChebyshevBinaryFormat.ReadRecord(stream, seg, 1);
        Assert.Equal(8, rec.Length);
        Assert.Equal(350.0, rec[0], 12);
        Assert.Equal(2.0, rec[2], 12);
    }

    [Fact]
    public void MinimalFile_BadMagic_Rejected()
    {
        string path = Track(TempFile());
        WriteMinimalFile(path, version: 1, recordCount: 1);
        var bytes = File.ReadAllBytes(path);
        bytes[0] = (byte)'X';
        File.WriteAllBytes(path, bytes);
        Assert.Throws<InvalidDataException>(() => ChebyshevBinaryFormat.ReadIndex(path));
    }

    [Fact]
    public void MinimalFile_UnsupportedVersion_Rejected()
    {
        string path = Track(TempFile());
        WriteMinimalFile(path, version: 2, recordCount: 1);
        var ex = Assert.Throws<InvalidDataException>(() => ChebyshevBinaryFormat.ReadIndex(path));
        Assert.Contains("version", ex.Message);
    }

    [Fact]
    public void MinimalFile_TruncatedData_Rejected()
    {
        string path = Track(TempFile());
        WriteMinimalFile(path, version: 1, recordCount: 2);
        using (var stream = File.OpenWrite(path))
            stream.SetLength(new FileInfo(path).Length - 8);
        Assert.Throws<InvalidDataException>(() => ChebyshevBinaryFormat.ReadIndex(path));
    }

    [Fact]
    public void MinimalFile_TruncatedHeader_Rejected()
    {
        string path = Track(TempFile());
        File.WriteAllBytes(path, new byte[] { (byte)'S', (byte)'N', (byte)'E' });
        Assert.Throws<InvalidDataException>(() => ChebyshevBinaryFormat.ReadIndex(path));
    }

    [Fact]
    public void MinimalFile_DegreeLengthMismatch_Rejected()
    {
        string path = Track(TempFile());
        WriteMinimalFile(path, version: 1, recordCount: 1, corruptRecordLength: true);
        Assert.Throws<InvalidDataException>(() => ChebyshevBinaryFormat.ReadIndex(path));
    }

    [Fact]
    public void RealDataset_CorruptedMagic_RejectedByProvider()
    {
        string path = Track(TempFile());
        File.Copy(_datasetPath, path);
        using (var stream = new FileStream(path, FileMode.Open, FileAccess.Write, FileShare.Read))
        {
            stream.Seek(0, SeekOrigin.Begin);
            stream.WriteByte((byte)'X');
        }
        Assert.Throws<InvalidDataException>(() =>
            new IndexedChebyshevEphemerisProvider(path, expectedSha256: null));
    }

    private static void WriteMinimalFile(
        string path, int version, int recordCount, bool corruptRecordLength = false)
    {
        using var stream = File.OpenWrite(path);
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: false);
        writer.Write(Encoding.ASCII.GetBytes("SNEPH04B"));
        writer.Write(version);
        writer.Write(1); // segmentCount
        writer.Write((long)recordCount); // totalRecords
        byte[] prov = Encoding.UTF8.GetBytes("{\"source\":\"test\"}");
        writer.Write(prov.Length);
        writer.Write(prov);
        // Segment table entry.
        byte[] body = Encoding.UTF8.GetBytes("tiny");
        byte[] center = Encoding.UTF8.GetBytes("ssb");
        writer.Write(body.Length);
        writer.Write(body);
        writer.Write(center.Length);
        writer.Write(center);
        writer.Write(42); // targetId
        writer.Write(0); // centerId
        writer.Write(1); // frameId
        writer.Write(2); // segmentType
        writer.Write(1); // degree
        writer.Write(200.0); // interval
        writer.Write(50.0); // initEt
        writer.Write((long)recordCount);
        writer.Write(2451545.0); // coverageStartJd
        writer.Write(2451546.0); // coverageEndJd
        long dataOffset = stream.Position + 16;
        writer.Write(dataOffset);
        writer.Write(corruptRecordLength ? 999L : 8L); // recordLength (degree 1 -> 8)
        // Records: [MID, RADIUS, X0, X1, Y0, Y1, Z0, Z1].
        for (int i = 0; i < recordCount; i++)
        {
            double mid = 50.0 + (i + 0.5) * 200.0;
            writer.Write(mid);
            writer.Write(100.0);
            writer.Write(1.0 + i);
            writer.Write(0.0);
            writer.Write(2.0 + i);
            writer.Write(0.0);
            writer.Write(3.0 + i);
            writer.Write(0.0);
        }
    }
}
