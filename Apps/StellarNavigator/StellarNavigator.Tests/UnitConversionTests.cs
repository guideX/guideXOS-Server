using Astronomy.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class UnitConversionTests
{
    [Fact]
    public void Distance_KilometerToMeter()
    {
        var result = Distance.Convert(1.0, DistanceUnit.Kilometer, DistanceUnit.Meter);
        Assert.Equal(1000.0, result, 10);
    }

    [Fact]
    public void Distance_AuToKilometer()
    {
        var result = Distance.Convert(1.0, DistanceUnit.AstronomicalUnit, DistanceUnit.Kilometer);
        Assert.Equal(149_597_870.7, result, 1);
    }

    [Fact]
    public void Distance_RoundTrip()
    {
        var original = 42.0;
        var km = Distance.Convert(original, DistanceUnit.AstronomicalUnit, DistanceUnit.Kilometer);
        var back = Distance.Convert(km, DistanceUnit.Kilometer, DistanceUnit.AstronomicalUnit);
        Assert.Equal(original, back, 10);
    }

    [Fact]
    public void Mass_KilogramToSolarMass()
    {
        var result = Mass.Convert(1.98892e30, MassUnit.Kilogram, MassUnit.SolarMass);
        Assert.Equal(1.0, result, 5);
    }

    [Fact]
    public void Time_DayToSecond()
    {
        var result = Time.Convert(1.0, TimeUnit.Day, TimeUnit.Second);
        Assert.Equal(86400.0, result, 10);
    }

    [Fact]
    public void Time_JulianYearToDay()
    {
        var result = Time.Convert(1.0, TimeUnit.JulianYear, TimeUnit.Day);
        Assert.Equal(365.25, result, 10);
    }
}
