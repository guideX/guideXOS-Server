using System.Text.Json;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using Xunit;
using Xunit.Abstractions;

namespace StellarNavigator.Tests;

/// <summary>SN6R public-boundary regressions for origin, quality, time, and provenance contracts.</summary>
public sealed class Sn6rContractTests
{
    private static readonly AstroTime J2000 = new(2451545.0, TimeScale.Tdb);
    private readonly ITestOutputHelper _output;

    public Sn6rContractTests(ITestOutputHelper output) => _output = output;

    private static CartesianState State(string origin, NavigationResultStatus quality = NavigationResultStatus.ValidatedReference,
        bool simulated = false, double et = 0.0, AstroTime? epoch = null) => new()
    {
        Position = new Vector3D(10.0, 20.0, 30.0),
        Velocity = new Vector3D(1.0, 2.0, 3.0),
        Orientation = FrameOrientation.J2000,
        OriginId = origin,
        Epoch = epoch ?? new AstroTime(2451545.0 + et / 86400.0, TimeScale.Tdb),
        EtSeconds = et,
        Provenance = "SN6R contract fixture",
        IsSimulated = simulated,
        Quality = quality,
    };

    [Fact]
    public void OriginTranslation_RequiresSharedSourceOrigin_AndKeepsRequestedResultOrigin()
    {
        var target = State("ssb");
        var origin = State("earth");
        var mismatch = Assert.Throws<InvalidOperationException>(
            () => OriginTranslation.ExpressRelativeTo(target, origin, "mars"));
        Assert.Contains("source coordinate origins", mismatch.Message, StringComparison.OrdinalIgnoreCase);

        var commonTarget = WithPosition(State("ssb"), new Vector3D(100, 20, 30));
        var commonOrigin = WithPosition(State("ssb"), new Vector3D(10, 5, 4));
        var relative = OriginTranslation.ExpressRelativeTo(commonTarget, commonOrigin, "earth");
        Assert.Equal(new Vector3D(90, 15, 26), relative.Position);
        Assert.Equal(new Vector3D(0, 0, 0), relative.Velocity);
        Assert.Equal("earth", relative.OriginId);
    }

    [Fact]
    public void OriginTranslation_RejectsInconsistentEpochAndScaleMetadata()
    {
        var target = State("ssb");
        var differentInstant = State("ssb", et: 0.01);
        Assert.Throws<InvalidOperationException>(
            () => OriginTranslation.ExpressRelativeTo(target, differentInstant, "earth"));

        var wrongEt = State("ssb", et: 0.002, epoch: J2000);
        Assert.Throws<ArgumentException>(
            () => OriginTranslation.ExpressRelativeTo(target, wrongEt, "earth"));

        var wrongScale = State("ssb", epoch: new AstroTime(2451545.0, TimeScale.Utc));
        Assert.Throws<ArgumentException>(
            () => OriginTranslation.ExpressRelativeTo(target, wrongScale, "earth"));
    }

    [Fact]
    public void OriginTranslation_RejectsNonSiUnitMetadata()
    {
        var invalidUnits = WithUnits(State("ssb"), (Sn6PositionUnit)99, (Sn6VelocityUnit)99);
        Assert.Throws<InvalidOperationException>(() =>
            OriginTranslation.ExpressRelativeTo(State("ssb"), invalidUnits, "earth"));
    }

    [Fact]
    public void OriginTranslation_QualityMatrixIsConservativeForEveryOrderedPair()
    {
        var all = Enum.GetValues<NavigationResultStatus>();
        foreach (var targetQuality in all)
        foreach (var originQuality in all)
        foreach (bool targetSimulated in new[] { false, true })
        foreach (bool originSimulated in new[] { false, true })
        {
            var target = State("ssb", targetQuality, targetSimulated);
            var origin = State("ssb", originQuality, originSimulated);
            bool failed = IsFailure(targetQuality) || IsFailure(originQuality);
            if (failed)
            {
                Assert.Throws<InvalidOperationException>(
                    () => OriginTranslation.ExpressRelativeTo(target, origin, "earth"));
                continue;
            }

            var result = OriginTranslation.ExpressRelativeTo(target, origin, "earth");
            var expected = targetQuality == NavigationResultStatus.StaleData || originQuality == NavigationResultStatus.StaleData
                ? NavigationResultStatus.StaleData
                : targetSimulated || originSimulated ||
                  targetQuality == NavigationResultStatus.ApproximateOrSimulated ||
                  originQuality == NavigationResultStatus.ApproximateOrSimulated
                    ? NavigationResultStatus.ApproximateOrSimulated
                    : NavigationResultStatus.ValidatedReference;
            Assert.Equal(expected, result.Quality);
            Assert.Equal(targetSimulated || originSimulated, result.IsSimulated);
        }
    }

    [Fact]
    public void OriginTranslation_StaleAndSimulationMetadataSurviveInEitherOrdering()
    {
        var validated = State("ssb");
        var staleSimulated = State("ssb", NavigationResultStatus.StaleData, simulated: true);
        var forward = OriginTranslation.ExpressRelativeTo(validated, staleSimulated, "sc:1");
        var reverse = OriginTranslation.ExpressRelativeTo(staleSimulated, validated, "sc:1");
        Assert.Equal(NavigationResultStatus.StaleData, forward.Quality);
        Assert.Equal(NavigationResultStatus.StaleData, reverse.Quality);
        Assert.True(forward.IsSimulated);
        Assert.True(reverse.IsSimulated);
    }

    [Fact]
    public void OrientationService_RequiresVerifiedModelOnlyForEarthFixedResults()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var unconfigured = new FramedStateService(provider);
        var inertial = unconfigured.GetState("earth", J2000, FrameOrientation.J2000, "ssb");
        Assert.True(inertial.IsSuccess, inertial.ErrorMessage);

        var earthFixed = unconfigured.GetState("earth", J2000, FrameOrientation.IauEarth, "ssb");
        Assert.False(earthFixed.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, earthFixed.Status);
        Assert.Contains("authenticated", earthFixed.ErrorMessage, StringComparison.OrdinalIgnoreCase);

        var unverified = new FramedStateService(provider, IauEarthOrientation.Instance);
        Assert.False(IauEarthOrientation.Instance.IsAuthenticated);
        Assert.Throws<InvalidOperationException>(() =>
            IauEarthOrientation.Instance.GetRotation(0.0, out _, out _));
        Assert.False(unverified.GetState("earth", J2000, FrameOrientation.IauEarth, "ssb").IsSuccess);

        var verified = IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath());
        Assert.True(verified.IsAuthenticated);
        var configured = new FramedStateService(provider, verified);
        Assert.True(configured.GetState("earth", J2000, FrameOrientation.IauEarth, "ssb").IsSuccess);
    }

    [Fact]
    public void OrientationAsset_CorruptionFailsBeforeAValidatedServiceCanBeCreated()
    {
        string temp = Path.Combine(Path.GetTempPath(), $"sn6r-orientation-{Guid.NewGuid():N}.json");
        try
        {
            File.WriteAllText(temp, "{}");
            var ex = Assert.Throws<InvalidDataException>(() => IauEarthOrientation.Load(temp));
            Assert.Contains("integrity", ex.Message, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            if (File.Exists(temp)) File.Delete(temp);
        }
    }

    [Theory]
    [InlineData(-1, 1, 1, 0, 0, 0, 0)]
    [InlineData(10000, 1, 1, 0, 0, 0, 0)]
    [InlineData(2019, 0, 1, 0, 0, 0, 0)]
    [InlineData(2019, 13, 1, 0, 0, 0, 0)]
    [InlineData(2019, 2, 29, 0, 0, 0, 0)]
    [InlineData(2019, 4, 31, 0, 0, 0, 0)]
    [InlineData(2019, 1, 1, -1, 0, 0, 0)]
    [InlineData(2019, 1, 1, 24, 0, 0, 0)]
    [InlineData(2019, 1, 1, 0, -1, 0, 0)]
    [InlineData(2019, 1, 1, 0, 60, 0, 0)]
    [InlineData(2019, 1, 1, 0, 0, -1, 0)]
    [InlineData(2019, 1, 1, 0, 0, 61, 0)]
    [InlineData(2019, 1, 1, 0, 0, 60, 0)]
    public void UtcInstantConstructor_RejectsInvalidCalendarAndLeapPlacement(
        int year, int month, int day, int hour, int minute, int second, double fraction)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new UtcInstant(year, month, day, hour, minute, second, fraction));
    }

    [Theory]
    [InlineData(-0.1)]
    [InlineData(1.0)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    [InlineData(double.NegativeInfinity)]
    public void UtcInstantConstructor_RejectsInvalidFractions(double fraction)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new UtcInstant(2016, 12, 31, 23, 59, 59, fraction));
    }

    [Fact]
    public void UtcObjectConversion_RejectsDefaultStructAndValidatesLeapTable()
    {
        var converter = Sn5Paths.LoadConverter();
        Assert.False(converter.TryUtcToEt(default(UtcInstant), out _, out var uninitialized));
        Assert.Equal(TimeConversionQuality.InvalidUtcInput, uninitialized!.Quality);
        Assert.Contains("month", uninitialized.Message, StringComparison.OrdinalIgnoreCase);

        var realLeap = new UtcInstant(2016, 12, 31, 23, 59, 60);
        Assert.True(converter.TryUtcToEt(realLeap, out var result, out _));
        Assert.Equal(realLeap, result!.Utc);

        var syntacticallyValidButUnannounced = new UtcInstant(2016, 6, 30, 23, 59, 60);
        Assert.False(converter.TryUtcToEt(syntacticallyValidButUnannounced, out _, out var unsupported));
        Assert.Equal(TimeConversionQuality.InvalidUtcInput, unsupported!.Quality);
    }

    [Fact]
    public void UtcAndFramedQueries_RetainEtPrecisionAndProvisionalTrust()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var converter = Sn5Paths.LoadConverter();
        var query = new UtcEphemerisQuery(provider, converter);
        const string utc = "2016-07-14T00:00:00.123456789Z";

        var resolved = query.GetState("earth", utc, ReferenceFrame.Barycentric);
        Assert.True(resolved.IsSuccess);
        Assert.Equal(TimeConversionQuality.Validated, resolved.TimeQuality);
        Assert.Equal(TimeRepresentationPrecision.EtSeconds, resolved.RepresentationPrecision);
        double jdEt = IndexedChebyshevEphemerisProvider.ToEphemerisSeconds(
            new AstroTime(resolved.Time!.JdTdb, TimeScale.Tdb));
        double quantization = jdEt - resolved.Time.Et.Value;
        Assert.True(Math.Abs(quantization) >= 1e-5, $"JD quantization was only {quantization:E3}s.");

        var exact = provider.GetStateAtEt("earth", resolved.Time.Et, ReferenceFrame.Barycentric);
        var legacy = provider.GetState("earth", new AstroTime(resolved.Time.JdTdb, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.True(exact.IsSuccess && legacy.IsSuccess);
        Assert.Equal(exact.Value.Position, resolved.Ephemeris!.Value.Position);
        double positionDelta = (exact.Value.Position - legacy.Value.Position).Magnitude;
        _output.WriteLine($"UTC {utc}: ET={resolved.Time.Et.Value:R}s, JD-derived ET={jdEt:R}s, " +
            $"delta={quantization * 1e6:F3} us, evaluated position delta={positionDelta:F6} m.");
        Assert.True(positionDelta > 0.1,
            "Expected JD quantization to measurably alter the evaluated Earth state.");

        var framed = new FramedStateService(provider).GetStateUtc("earth", "2024-01-01T00:00:00Z",
            converter, FrameOrientation.J2000, "ssb", allowProvisionalFuture: true);
        Assert.True(framed.IsSuccess);
        Assert.Equal(NavigationResultStatus.StaleData, framed.Value!.Quality);
        Assert.Equal(NavigationResultStatus.StaleData, framed.Status);
        Assert.Contains("provisional", framed.Value.Provenance, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ET=", framed.Provenance, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void EtProviderPath_MatchesIndependentCspiceType2Fixture()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        using var json = JsonDocument.Parse(File.ReadAllText(Sn4Paths.Sn4ReferencePath()));
        var fixture = json.RootElement.GetProperty("cases").EnumerateArray()
            .First(c => c.GetProperty("case").GetString() == "random_offgrid" &&
                        c.GetProperty("bodyId").GetString() == "saturn" &&
                        c.GetProperty("observerId").GetString() == "ssb");
        double et = fixture.GetProperty("etSec").GetDouble();
        double jdTdb = fixture.GetProperty("jdTdb").GetDouble();
        var expectedPos = fixture.GetProperty("posKm").EnumerateArray().Select(x => x.GetDouble()).ToArray();
        var expectedVel = fixture.GetProperty("velKms").EnumerateArray().Select(x => x.GetDouble()).ToArray();

        var result = provider.GetStateAtEt("saturn", new EtSeconds(et), ReferenceFrame.Barycentric);
        Assert.True(result.IsSuccess, result.ErrorMessage);
        var (pos, vel) = result.Value;
        double posErrKm = new[] {
            Math.Abs(pos.X / 1000.0 - expectedPos[0]), Math.Abs(pos.Y / 1000.0 - expectedPos[1]),
            Math.Abs(pos.Z / 1000.0 - expectedPos[2]) }.Max();
        double velErrKms = new[] {
            Math.Abs(vel.X / 1000.0 - expectedVel[0]), Math.Abs(vel.Y / 1000.0 - expectedVel[1]),
            Math.Abs(vel.Z / 1000.0 - expectedVel[2]) }.Max();
        _output.WriteLine($"CSPICE sn4_reference ET path: saturn ET={et:R}s, max position={posErrKm:E3} km, " +
            $"max velocity={velErrKms:E3} km/s.");
        Assert.True(posErrKm <= 1e-6);
        Assert.True(velErrKms <= 1e-9);

        var legacyJdResult = provider.GetState("saturn", new AstroTime(jdTdb, TimeScale.Tdb), ReferenceFrame.Barycentric);
        Assert.True(legacyJdResult.IsSuccess, legacyJdResult.ErrorMessage);
        var (legacyPos, legacyVel) = legacyJdResult.Value;
        double legacyPosErrKm = new[] {
            Math.Abs(legacyPos.X / 1000.0 - expectedPos[0]), Math.Abs(legacyPos.Y / 1000.0 - expectedPos[1]),
            Math.Abs(legacyPos.Z / 1000.0 - expectedPos[2]) }.Max();
        double legacyVelErrKms = new[] {
            Math.Abs(legacyVel.X / 1000.0 - expectedVel[0]), Math.Abs(legacyVel.Y / 1000.0 - expectedVel[1]),
            Math.Abs(legacyVel.Z / 1000.0 - expectedVel[2]) }.Max();
        _output.WriteLine($"Legacy JD ET={(IndexedChebyshevEphemerisProvider.ToEphemerisSeconds(new AstroTime(jdTdb, TimeScale.Tdb))):R}s; " +
            $"CSPICE reference error position={legacyPosErrKm:E3} km, velocity={legacyVelErrKms:E3} km/s.");
        Assert.True(legacyPosErrKm <= 1e-6);
        Assert.True(legacyVelErrKms <= 1e-9);

        var fullProvider = new ChebyshevEphemerisProvider(ChebyshevProviderTests.ResolveDatasetPath());
        var commonEt = new EtSeconds(123456789.123);
        var indexedCommon = provider.GetStateAtEt("jupiter", commonEt, ReferenceFrame.Barycentric);
        var fullResult = fullProvider.GetStateAtEt("jupiter", commonEt, ReferenceFrame.Barycentric);
        Assert.True(fullResult.IsSuccess, fullResult.ErrorMessage);
        Assert.True(indexedCommon.IsSuccess, indexedCommon.ErrorMessage);
        Assert.Equal(indexedCommon.Value.Position, fullResult.Value.Position);
        Assert.Equal(indexedCommon.Value.Velocity, fullResult.Value.Velocity);
    }

    [Fact]
    public void LegacyTdbTime_RejectsImplicitUtcButKeepsTdbCompatibility()
    {
        var tdb = new AstroTime(2451545.5, TimeScale.Tdb);
        Assert.Equal(tdb, TdbTime.UtcToTdb(tdb));

        var utcLabel = new AstroTime(2451545.5, TimeScale.Utc);
        Assert.Throws<NotSupportedException>(() => TdbTime.UtcToTdb(utcLabel));
        Assert.Throws<NotSupportedException>(() => TdbTime.TdbToUtc(tdb));
        Assert.Throws<ArgumentException>(() => IndexedChebyshevEphemerisProvider.ToEphemerisSeconds(utcLabel));

        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var result = provider.GetState("earth", utcLabel, ReferenceFrame.Barycentric);
        Assert.False(result.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, result.Status);
        Assert.Contains("TDB", result.ErrorMessage);

        var fullProvider = new ChebyshevEphemerisProvider(ChebyshevProviderTests.ResolveDatasetPath());
        var fullResult = fullProvider.GetState("earth", utcLabel, ReferenceFrame.Barycentric);
        Assert.False(fullResult.IsSuccess);
        Assert.Equal(NavigationResultStatus.InvalidInput, fullResult.Status);
    }

    private static bool IsFailure(NavigationResultStatus status) => status is
        NavigationResultStatus.InsufficientInformation or
        NavigationResultStatus.UnsupportedCalculation or
        NavigationResultStatus.InvalidInput;

    private static CartesianState WithPosition(CartesianState state, Vector3D position) => new()
    {
        Position = position,
        Velocity = state.Velocity,
        Orientation = state.Orientation,
        OriginId = state.OriginId,
        Epoch = state.Epoch,
        EtSeconds = state.EtSeconds,
        Provenance = state.Provenance,
        IsSimulated = state.IsSimulated,
        Quality = state.Quality,
    };

    private static CartesianState WithUnits(CartesianState state, Sn6PositionUnit positionUnit,
        Sn6VelocityUnit velocityUnit) => new()
    {
        Position = state.Position,
        Velocity = state.Velocity,
        Orientation = state.Orientation,
        OriginId = state.OriginId,
        Epoch = state.Epoch,
        EtSeconds = state.EtSeconds,
        PositionUnit = positionUnit,
        VelocityUnit = velocityUnit,
        Provenance = state.Provenance,
        IsSimulated = state.IsSimulated,
        Quality = state.Quality,
    };
}
