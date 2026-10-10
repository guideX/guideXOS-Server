using Astronomy.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class TimeBoundaryTests
{
    [Fact]
    public void AstroTime_FromJ2000Days_ZeroIsJ2000()
    {
        var t = AstroTime.FromJ2000Days(0);
        Assert.Equal(2451545.0, t.JdUtc, 6);
    }

    [Fact]
    public void AstroTime_AddDays()
    {
        var t = AstroTime.FromJ2000Days(0);
        var later = t.AddDays(1);
        Assert.Equal(1.0, later.DifferenceInDays(t), 10);
    }

    [Fact]
    public void AstroTime_FromIsoUtc_RoundTrip()
    {
        var t = AstroTime.FromIsoUtc("2000-01-01T12:00:00Z");
        Assert.Equal(2451545.0, t.JdUtc, 4);
    }

    [Fact]
    public void AstroTime_DaysSinceJ2000()
    {
        var t = AstroTime.FromJ2000Days(10);
        Assert.Equal(10.0, t.DaysSinceJ2000, 10);
    }

    [Fact]
    public void AstroTime_CompareTo()
    {
        var a = AstroTime.FromJ2000Days(0);
        var b = AstroTime.FromJ2000Days(1);
        Assert.True(a.CompareTo(b) < 0);
        Assert.True(b.CompareTo(a) > 0);
        Assert.Equal(0, a.CompareTo(AstroTime.FromJ2000Days(0)));
    }

    [Fact]
    public void AstroTime_OriginalScalePreserved()
    {
        var t = AstroTime.FromJdUtc(2451545.0);
        Assert.Equal(TimeScale.Utc, t.OriginalScale);
    }
}
