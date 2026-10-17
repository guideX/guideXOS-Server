using Astronomy.Core;

namespace Astronomy.Ephemeris;

/// <summary>
/// Historical SN1/SN2 time convenience helper. UTC conversion is intentionally
/// disabled because it predates the authenticated SN5 leap-second policy;
/// TDB-only compatibility helpers remain available.
/// </summary>
public static class TdbTime
{
    public const double J2000Jd = 2451545.0;
    public const double SecondsPerDay = 86400.0;
    public const double TdbMinusTtAtJ2000 = 0.0;
    public const double TdbTtMaxDriftPerSecond = 1.7e-9;

    public static AstroTime FromJ2000Days(double days) =>
        new(days + J2000Jd, TimeScale.Tdb);

    public static AstroTime FromJdUtc(double jdUtc) =>
        new(jdUtc, TimeScale.Utc);

    public static AstroTime FromIsoUtc(string isoUtc)
    {
        var dt = DateTime.Parse(isoUtc, null, System.Globalization.DateTimeStyles.RoundtripKind);
        return new AstroTime(ToJd(dt), TimeScale.Utc);
    }

    public static AstroTime FromIsoTdb(string isoTdb)
    {
        var dt = DateTime.Parse(isoTdb, null, System.Globalization.DateTimeStyles.RoundtripKind);
        return new AstroTime(ToJd(dt), TimeScale.Tdb);
    }

    public static double TdbMinusTt(AstroTime epoch)
    {
        var daysSinceJ2000 = epoch.JdUtc - J2000Jd;
        var secondsSinceJ2000 = daysSinceJ2000 * SecondsPerDay;
        return TdbMinusTtAtJ2000 + secondsSinceJ2000 * TdbTtMaxDriftPerSecond;
    }

    public static AstroTime UtcToTdb(AstroTime utcEpoch)
    {
        if (utcEpoch.OriginalScale == TimeScale.Tdb)
            return utcEpoch;
        throw new NotSupportedException(
            "The legacy linear-drift UTC-to-TDB approximation is disabled. Convert UTC through the authenticated SN5 TimeConverter.");
    }

    public static AstroTime TdbToUtc(AstroTime tdbEpoch)
    {
        if (tdbEpoch.OriginalScale == TimeScale.Utc)
            return tdbEpoch;
        throw new NotSupportedException(
            "The legacy linear-drift TDB-to-UTC approximation is disabled. Convert through the authenticated SN5 TimeConverter.");
    }

    private static double ToJd(DateTime dt)
    {
        var utc = dt.ToUniversalTime();
        var y = utc.Year;
        var m = utc.Month;
        var d = utc.Day + utc.Hour / 24.0 + utc.Minute / 1440.0 + utc.Second / 86400.0;

        if (m <= 2)
        {
            y -= 1;
            m += 12;
        }

        var a = Math.Floor(y / 100.0);
        var b = 2 - a + Math.Floor(a / 4.0);
        return Math.Floor(365.25 * (y + 4716)) + Math.Floor(30.6001 * (m + 1)) + d + b - 1524.5;
    }
}
