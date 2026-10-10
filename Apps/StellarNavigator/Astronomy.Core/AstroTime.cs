namespace Astronomy.Core;

public enum TimeScale
{
    Utc,
    Tai,
    Tt,
    Tdb
}

public readonly struct AstroTime : IComparable<AstroTime>, IEquatable<AstroTime>
{
    public double JdUtc { get; }
    public TimeScale OriginalScale { get; }

    private AstroTime(double jdUtc, TimeScale originalScale)
    {
        JdUtc = jdUtc;
        OriginalScale = originalScale;
    }

    public static AstroTime FromJdUtc(double jd) => new(jd, TimeScale.Utc);

    public static AstroTime FromIsoUtc(string isoUtc)
    {
        var dt = DateTime.Parse(isoUtc, null, System.Globalization.DateTimeStyles.RoundtripKind);
        return new(ToJd(dt), TimeScale.Utc);
    }

    public static AstroTime FromJ2000Days(double daysSinceJ2000) =>
        new(daysSinceJ2000 + 2451545.0, TimeScale.Tdb);

    public double DaysSinceJ2000 => JdUtc - 2451545.0;

    public AstroTime AddDays(double days) => new(JdUtc + days, OriginalScale);

    public double DifferenceInDays(AstroTime other) => JdUtc - other.JdUtc;

    public string ToIsoString() =>
        JdToDateTime(JdUtc).ToString("yyyy-MM-ddTHH:mm:ssZ", System.Globalization.CultureInfo.InvariantCulture);

    public int CompareTo(AstroTime other) => JdUtc.CompareTo(other.JdUtc);

    public bool Equals(AstroTime other) => JdUtc.Equals(other.JdUtc);

    public override bool Equals(object? obj) => obj is AstroTime other && Equals(other);

    public override int GetHashCode() => JdUtc.GetHashCode();

    public override string ToString() =>
        $"JD {JdUtc:F6} UTC (original scale: {OriginalScale})";

    internal static double ToJd(DateTime dt)
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

    internal DateTime JdToDateTime(double jd)
    {
        var z = Math.Floor(jd + 0.5);
        var f = jd + 0.5 - z;
        double a;

        if (z < 2299161)
        {
            a = z;
        }
        else
        {
            var alpha = Math.Floor((z - 1867216.25) / 36524.25);
            a = z + 1 + alpha - Math.Floor(alpha / 4.0);
        }

        var b = a + 1524;
        var c = Math.Floor((b - 122.1) / 365.25);
        var d = Math.Floor(365.25 * c);
        var e = Math.Floor((b - d) / 30.6001);

        var day = b - d - Math.Floor(30.6001 * e) + f;
        var month = e < 14 ? e - 1 : e - 13;
        var year = month > 2 ? c - 4716 : c - 4715;

        var dayInt = (int)Math.Floor(day);
        var frac = day - dayInt;
        var hour = (int)(frac * 24);
        var minute = (int)((frac * 24 - hour) * 60);
        var second = (int)(((frac * 24 - hour) * 60 - minute) * 60);

        return new DateTime((int)year, (int)month, dayInt, hour, minute, second, DateTimeKind.Utc);
    }
}
