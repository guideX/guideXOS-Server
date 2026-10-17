using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// SN6 astronomical-time integration: ET-direct queries plus UTC queries
/// exclusively through the SN5 conversion policies (strict validated UTC,
/// provisional opt-in with downgrade, pre-1972 refusal, leap-second
/// handling). Provisional epochs are never silently validated; JD TDB
/// labels carry the documented representation limit.
/// </summary>
public class Sn6TimeIntegrationTests
{
    private static TimeConverter Converter()
    {
        var provider = LeapSecondProvider.Load(Sn5Paths.TimeDataAssetPath());
        return new TimeConverter(provider.Table, provider.Identity);
    }

    private static FramedStateService Service(out IndexedChebyshevEphemerisProvider provider)
    {
        provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        return new FramedStateService(provider, IauEarthOrientation.Load(Sn6Paths.OrientationAssetPath()));
    }

    [Fact]
    public void J2000_WorksWithoutConverterOrOrientationAsset()
    {
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var epoch = new AstroTime(2451545.0, TimeScale.Tdb);
        var j2000 = svc.GetState("earth", epoch, FrameOrientation.J2000, "ssb");
        Assert.True(j2000.IsSuccess, j2000.ErrorMessage);
        Assert.Equal(FrameOrientation.J2000, j2000.Value!.Orientation);

        var rotating = svc.GetState("earth", epoch, FrameOrientation.IauEarth, "ssb");
        Assert.False(rotating.IsSuccess);
        Assert.Equal(NavigationResultStatus.InsufficientInformation, rotating.Status);
        Assert.Contains("authenticated", rotating.ErrorMessage, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void StrictUtc_QuerySucceeds_Validated()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var r = svc.GetStateUtc("earth", "2016-07-14T00:00:00Z", Converter(),
                FrameOrientation.J2000, "ssb");
            Assert.True(r.IsSuccess, r.ErrorMessage);
            Assert.Equal(NavigationResultStatus.ValidatedReference, r.Value!.Quality);

            // Both SN5 query surfaces retain the exact converted ET.
            var q = new UtcEphemerisQuery(provider, Converter());
            var check = q.GetState("earth", "2016-07-14T00:00:00Z", ReferenceFrame.Barycentric, false);
            Assert.True(check.IsSuccess);
            Assert.Equal(check.Ephemeris!.Value.Position, r.Value.Position);
            Assert.Equal(TimeRepresentationPrecision.EtSeconds, check.RepresentationPrecision);
            Assert.Equal(check.Time!.Et.Value, r.Value.EtSeconds);
        }
    }

    [Fact]
    public void StrictUtc_EarthFixed_Works()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var r = svc.GetStateUtc("mars", "2016-07-14T00:00:00Z", Converter(),
                FrameOrientation.IauEarth, "earth");
            Assert.True(r.IsSuccess, r.ErrorMessage);
            Assert.Equal(FrameOrientation.IauEarth, r.Value!.Orientation);
            Assert.Equal("earth", r.Value.OriginId);
        }
    }

    [Fact]
    public void ProvisionalUtc_RequiresOptIn_AndDowngrades()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var strict = svc.GetStateUtc("earth", "2030-01-01T00:00:00Z", Converter(),
                FrameOrientation.J2000, "ssb", allowProvisionalFuture: false);
            Assert.False(strict.IsSuccess);

            var prov = svc.GetStateUtc("earth", "2030-01-01T00:00:00Z", Converter(),
                FrameOrientation.J2000, "ssb", allowProvisionalFuture: true);
            Assert.True(prov.IsSuccess, prov.ErrorMessage);
            Assert.Equal(NavigationResultStatus.StaleData, prov.Value!.Quality);
            Assert.Contains("provisional", prov.Value.Provenance);
        }
    }

    [Fact]
    public void Pre1972Utc_Refused()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var r = svc.GetStateUtc("earth", "1969-07-20T20:17:40Z", Converter(),
                FrameOrientation.J2000, "ssb", allowProvisionalFuture: true);
            Assert.False(r.IsSuccess);
        }
    }

    [Fact]
    public void InvalidLeapSecond_Rejected()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var r = svc.GetStateUtc("earth", "2019-12-31T23:59:60Z", Converter(),
                FrameOrientation.J2000, "ssb");
            Assert.False(r.IsSuccess);
        }
    }

    [Fact]
    public void LeapSecondInstant_StrictQueryWorks()
    {
        var svc = Service(out var provider);
        using (provider)
        {
            var r = svc.GetStateUtc("earth", "2016-12-31T23:59:60Z", Converter(),
                FrameOrientation.J2000, "ssb");
            Assert.True(r.IsSuccess, r.ErrorMessage);
        }
    }

    [Fact]
    public void JdTdb_LabelCarriesDocumentedLimit()
    {
        // Direct AstroTime compatibility queries use the same JD-derived ET
        // for provider evaluation and frame orientation.
        using var provider = new IndexedChebyshevEphemerisProvider(Sn4Paths.DatasetPath());
        var svc = new FramedStateService(provider);
        var epoch = new AstroTime(2457583.5, TimeScale.Tdb);
        var framed = svc.GetState("jupiter", epoch, FrameOrientation.J2000, "ssb");
        var direct = provider.GetState("jupiter", epoch, ReferenceFrame.Barycentric);
        Assert.True(framed.IsSuccess && direct.IsSuccess);
        Assert.Equal(direct.Value!.Position, framed.Value!.Position);
    }
}
