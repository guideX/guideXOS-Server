namespace Astronomy.Core;

public enum DistanceUnit
{
    Meter,
    Kilometer,
    AstronomicalUnit,
    LightSecond,
    LightYear,
    Parsec
}

public enum MassUnit
{
    Kilogram,
    Gram,
    SolarMass,
    EarthMass,
    JupiterMass
}

public enum TimeUnit
{
    Second,
    Minute,
    Hour,
    Day,
    JulianYear
}

public static class Distance
{
    public const double MetersPerKilometer = 1_000.0;
    public const double MetersPerAstronomicalUnit = 149_597_870_700.0;
    public const double MetersPerLightSecond = 299_792_458.0;
    public const double MetersPerLightYear = 9_460_730_472_580_800.0;
    public const double MetersPerParsec = 3.0856775814913673e16;

    public static double Convert(double value, DistanceUnit from, DistanceUnit to)
    {
        if (from == to) return value;
        var meters = value * GetMetersPerUnit(from);
        return meters / GetMetersPerUnit(to);
    }

    private static double GetMetersPerUnit(DistanceUnit unit) => unit switch
    {
        DistanceUnit.Meter => 1.0,
        DistanceUnit.Kilometer => MetersPerKilometer,
        DistanceUnit.AstronomicalUnit => MetersPerAstronomicalUnit,
        DistanceUnit.LightSecond => MetersPerLightSecond,
        DistanceUnit.LightYear => MetersPerLightYear,
        DistanceUnit.Parsec => MetersPerParsec,
        _ => throw new ArgumentOutOfRangeException(nameof(unit))
    };
}

public static class Mass
{
    public const double KilogramsPerGram = 0.001;
    public const double KilogramsPerSolarMass = 1.98892e30;
    public const double KilogramsPerEarthMass = 5.9722e24;
    public const double KilogramsPerJupiterMass = 1.89813e27;

    public static double Convert(double value, MassUnit from, MassUnit to)
    {
        if (from == to) return value;
        var kg = value * GetKilogramsPerUnit(from);
        return kg / GetKilogramsPerUnit(to);
    }

    private static double GetKilogramsPerUnit(MassUnit unit) => unit switch
    {
        MassUnit.Kilogram => 1.0,
        MassUnit.Gram => KilogramsPerGram,
        MassUnit.SolarMass => KilogramsPerSolarMass,
        MassUnit.EarthMass => KilogramsPerEarthMass,
        MassUnit.JupiterMass => KilogramsPerJupiterMass,
        _ => throw new ArgumentOutOfRangeException(nameof(unit))
    };
}

public static class Time
{
    public const double SecondsPerMinute = 60.0;
    public const double SecondsPerHour = 3_600.0;
    public const double SecondsPerDay = 86_400.0;
    public const double SecondsPerJulianYear = 365.25 * SecondsPerDay;

    public static double Convert(double value, TimeUnit from, TimeUnit to)
    {
        if (from == to) return value;
        var seconds = value * GetSecondsPerUnit(from);
        return seconds / GetSecondsPerUnit(to);
    }

    private static double GetSecondsPerUnit(TimeUnit unit) => unit switch
    {
        TimeUnit.Second => 1.0,
        TimeUnit.Minute => SecondsPerMinute,
        TimeUnit.Hour => SecondsPerHour,
        TimeUnit.Day => SecondsPerDay,
        TimeUnit.JulianYear => SecondsPerJulianYear,
        _ => throw new ArgumentOutOfRangeException(nameof(unit))
    };
}
