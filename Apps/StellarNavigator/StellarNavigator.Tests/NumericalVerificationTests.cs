using System.Text.Json;
using System.Text.Json.Serialization;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class NumericalVerificationTests : IDisposable
{
    private readonly string _tempDir;
    private readonly string _ephemerisPath;
    private readonly List<ReferenceEntry> _reference;

    public NumericalVerificationTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "sn2_verify_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);

        var srcPath = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "Astronomy.Ephemeris", "Data", "ephemeris_de440s.json");
        if (!File.Exists(srcPath))
            srcPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s.json");
        _ephemerisPath = Path.Combine(_tempDir, "ephemeris_de440s.json");
        File.Copy(srcPath, _ephemerisPath);

        var refPath = Path.Combine(AppContext.BaseDirectory, "sn2_reference.json");
        var refJson = File.ReadAllText(refPath);
        _reference = JsonSerializer.Deserialize<List<ReferenceEntry>>(refJson)!;
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    [Fact]
    public void EarthPosition_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var result = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);

        var (pos, vel) = result.Value!;
        var referenceEntry = _reference.First(r => r.Type == "body" && r.BodyId == "earth" && Math.Abs(r.Jd - 2451545.0) < 0.001);

        var posErrorKm = Math.Abs(pos.Magnitude / Distance.MetersPerKilometer - referenceEntry.PosMagnitudeKm);
        Assert.True(posErrorKm < 1.0, $"Earth position error {posErrorKm:F6} km exceeds 1 km tolerance");

        var velErrorKms = Math.Abs(vel.Magnitude / Distance.MetersPerKilometer - referenceEntry.VelMagnitudeMs / Distance.MetersPerKilometer);
        Assert.True(velErrorKms < 0.001, $"Earth velocity error {velErrorKms:F9} km/s exceeds 0.001 km/s tolerance");
    }

    [Fact]
    public void MoonPosition_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var result = provider.GetState("moon", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);

        var (pos, _) = result.Value!;
        var referenceEntry = _reference.First(r => r.Type == "body" && r.BodyId == "moon" && Math.Abs(r.Jd - 2451545.0) < 0.001);

        var posErrorKm = Math.Abs(pos.Magnitude / Distance.MetersPerKilometer - referenceEntry.PosMagnitudeKm);
        Assert.True(posErrorKm < 1.0, $"Moon position error {posErrorKm:F6} km exceeds 1 km tolerance");
    }

    [Fact]
    public void MarsPosition_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var result = provider.GetState("mars", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);

        var (pos, _) = result.Value!;
        var referenceEntry = _reference.First(r => r.Type == "body" && r.BodyId == "mars" && Math.Abs(r.Jd - 2451545.0) < 0.001);

        var posErrorKm = Math.Abs(pos.Magnitude / Distance.MetersPerKilometer - referenceEntry.PosMagnitudeKm);
        Assert.True(posErrorKm < 1.0, $"Mars position error {posErrorKm:F6} km exceeds 1 km tolerance");
    }

    [Fact]
    public void JupiterPosition_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var result = provider.GetState("jupiter", epoch, ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess);

        var (pos, _) = result.Value!;
        var referenceEntry = _reference.First(r => r.Type == "body" && r.BodyId == "jupiter" && Math.Abs(r.Jd - 2451545.0) < 0.001);

        var posErrorKm = Math.Abs(pos.Magnitude / Distance.MetersPerKilometer - referenceEntry.PosMagnitudeKm);
        Assert.True(posErrorKm < 1.0, $"Jupiter position error {posErrorKm:F6} km exceeds 1 km tolerance");
    }

    [Fact]
    public void EarthMoonDistance_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var moon = provider.GetState("moon", epoch, ReferenceFrame.Barycentric);
        Assert.True(earth.IsSuccess && moon.IsSuccess);

        var (earthPos, _) = earth.Value!;
        var (moonPos, _) = moon.Value!;
        var distKm = earthPos.DistanceTo(moonPos) / Distance.MetersPerKilometer;

        var referenceEntry = _reference.First(r => r.Type == "earth_moon_distance" && Math.Abs(r.Jd - 2451545.0) < 0.001);
        var error = Math.Abs(distKm - referenceEntry.DistanceKm);
        Assert.True(error < 1.0, $"Earth-Moon distance error {error:F3} km exceeds 1 km tolerance");
    }

    [Fact]
    public void EarthMarsDistance_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var mars = provider.GetState("mars", epoch, ReferenceFrame.Barycentric);
        Assert.True(earth.IsSuccess && mars.IsSuccess);

        var (earthPos, _) = earth.Value!;
        var (marsPos, _) = mars.Value!;
        var distKm = earthPos.DistanceTo(marsPos) / Distance.MetersPerKilometer;

        var referenceEntry = _reference.First(r => r.Type == "earth_mars_distance" && Math.Abs(r.Jd - 2451545.0) < 0.001);
        var error = Math.Abs(distKm - referenceEntry.DistanceKm);
        Assert.True(error < 10.0, $"Earth-Mars distance error {error:F3} km exceeds 10 km tolerance");
    }

    [Fact]
    public void EarthJupiterDistance_MatchesJplReference_AtJ2000()
    {
        var provider = new JplEphemerisProvider(_ephemerisPath);
        var epoch = AstroTime.FromJdUtc(2451545.0);
        var earth = provider.GetState("earth", epoch, ReferenceFrame.Barycentric);
        var jupiter = provider.GetState("jupiter", epoch, ReferenceFrame.Barycentric);
        Assert.True(earth.IsSuccess && jupiter.IsSuccess);

        var (earthPos, _) = earth.Value!;
        var (jupiterPos, _) = jupiter.Value!;
        var distKm = earthPos.DistanceTo(jupiterPos) / Distance.MetersPerKilometer;

        var referenceEntry = _reference.First(r => r.Type == "earth_jupiter_distance" && Math.Abs(r.Jd - 2451545.0) < 0.001);
        var error = Math.Abs(distKm - referenceEntry.DistanceKm);
        Assert.True(error < 10.0, $"Earth-Jupiter distance error {error:F3} km exceeds 10 km tolerance");
    }

    private sealed class ReferenceEntry
    {
        [JsonPropertyName("type")]
        public string Type { get; set; } = "";

        [JsonPropertyName("bodyId")]
        public string BodyId { get; set; } = "";

        [JsonPropertyName("jd")]
        public double Jd { get; set; }

        [JsonPropertyName("position_x")]
        public double PositionX { get; set; }

        [JsonPropertyName("position_y")]
        public double PositionY { get; set; }

        [JsonPropertyName("position_z")]
        public double PositionZ { get; set; }

        [JsonPropertyName("velocity_x")]
        public double VelocityX { get; set; }

        [JsonPropertyName("velocity_y")]
        public double VelocityY { get; set; }

        [JsonPropertyName("velocity_z")]
        public double VelocityZ { get; set; }

        [JsonPropertyName("distance_km")]
        public double DistanceKm { get; set; }

        public double PosMagnitudeKm => Math.Sqrt(PositionX * PositionX + PositionY * PositionY + PositionZ * PositionZ);

        public double VelMagnitudeMs => Math.Sqrt(VelocityX * VelocityX + VelocityY * VelocityY + VelocityZ * VelocityZ) * 1000.0;
    }
}
