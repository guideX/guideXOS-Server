namespace Astronomy.Time;

/// <summary>
/// Strict UTC calendar instant. Unlike platform DateTime, this type CAN
/// represent a positive leap second (second field 60, i.e. 23:59:60Z).
///
/// Only the strict ISO-8601-style form <c>YYYY-MM-DDTHH:MM:SS[.fraction]Z</c>
/// is accepted. Numeric timezone offsets are deferred to a later phase and
/// are rejected. Parsing is locale-independent (manual digit decoding) and
/// deterministic across operating systems.
///
/// A second value of 60 is accepted syntactically only as 23:59:60; whether
/// that leap second is real is decided against the authoritative
/// leap-second table by <see cref="TimeConverter"/> — an unsupported
/// 23:59:60 is never silently normalized into the following minute.
/// </summary>
public readonly struct UtcInstant : IEquatable<UtcInstant>, IComparable<UtcInstant>
{
    public int Year { get; }
    public int Month { get; }
    public int Day { get; }
    public int Hour { get; }
    public int Minute { get; }
    public int Second { get; }
    /// <summary>Fractional second in [0, 1).</summary>
    public double Fraction { get; }

    public bool IsLeapSecond => Second == 60;

    public UtcInstant(int year, int month, int day, int hour, int minute, int second, double fraction = 0.0)
    {
        Year = year;
        Month = month;
        Day = day;
        Hour = hour;
        Minute = minute;
        Second = second;
        Fraction = fraction;
    }

    /// <summary>
    /// Strict parse of <c>YYYY-MM-DDTHH:MM:SS[.fff]Z</c>.
    /// Returns false with a diagnostic message on any deviation.
    /// </summary>
    public static bool TryParse(string? text, out UtcInstant value, out string? error)
    {
        value = default;
        error = null;
        if (string.IsNullOrEmpty(text))
        {
            error = "UTC timestamp is empty.";
            return false;
        }

        // Minimal shape: "YYYY-MM-DDTHH:MM:SSZ" (20 chars), optional ".fff" before Z.
        if (text.Length < 20 || text[^1] != 'Z' || text[10] != 'T')
        {
            error = $"Unsupported UTC format {Quote(text)}: expected YYYY-MM-DDTHH:MM:SS[.fraction]Z.";
            return false;
        }
        if (text[4] != '-' || text[7] != '-' || text[13] != ':' || text[16] != ':')
        {
            error = $"Unsupported UTC format {Quote(text)}: expected YYYY-MM-DDTHH:MM:SS[.fraction]Z.";
            return false;
        }

        if (!TryDigits(text, 0, 4, out int year) || !TryDigits(text, 5, 2, out int month) ||
            !TryDigits(text, 8, 2, out int day) || !TryDigits(text, 11, 2, out int hour) ||
            !TryDigits(text, 14, 2, out int minute) || !TryDigits(text, 17, 2, out int second))
        {
            error = $"Unsupported UTC format {Quote(text)}: non-digit calendar field.";
            return false;
        }

        double fraction = 0.0;
        if (text.Length > 20)
        {
            if (text[19] != '.' || text.Length > 30)
            {
                error = $"Unsupported UTC format {Quote(text)}: fraction must be 1-9 digits.";
                return false;
            }
            int digits = text.Length - 21; // chars between '.' and 'Z'
            if (digits < 1 || digits > 9)
            {
                error = $"Unsupported UTC format {Quote(text)}: fraction must be 1-9 digits.";
                return false;
            }
            double scale = 0.1;
            for (int i = 20; i < text.Length - 1; i++)
            {
                char c = text[i];
                if (c < '0' || c > '9')
                {
                    error = $"Unsupported UTC format {Quote(text)}: non-digit fraction.";
                    return false;
                }
                fraction += (c - '0') * scale;
                scale *= 0.1;
            }
        }

        if (month < 1 || month > 12)
        {
            error = $"Invalid UTC month in {Quote(text)}.";
            return false;
        }
        int dim = CivilCalendar.DaysInMonth(year, month);
        if (day < 1 || day > dim)
        {
            error = $"Invalid UTC day in {Quote(text)}.";
            return false;
        }
        if (hour > 23)
        {
            error = $"Invalid UTC hour in {Quote(text)}.";
            return false;
        }
        if (minute > 59)
        {
            error = $"Invalid UTC minute in {Quote(text)}.";
            return false;
        }
        if (second > 60 || (second == 60 && (hour != 23 || minute != 59)))
        {
            error = $"Invalid UTC second in {Quote(text)}: second 60 is allowed only as 23:59:60.";
            return false;
        }
        if (fraction < 0.0 || fraction >= 1.0)
        {
            error = $"Invalid UTC fraction in {Quote(text)}.";
            return false;
        }

        value = new UtcInstant(year, month, day, hour, minute, second, fraction);
        return true;
    }

    /// <summary>
    /// Uniform civil seconds (86400 s/day, leap seconds NOT counted) since
    /// 2000-01-01T12:00:00 UTC. A 23:59:60 label maps to the same count as
    /// the following midnight; the physical extra second enters through the
    /// TAI-UTC offset step (see TimeConverter), matching CSPICE DELTET.
    /// </summary>
    internal double ToUniformSeconds()
    {
        long days = CivilCalendar.DaysSinceUnixEpoch(Year, Month, Day)
            - CivilCalendar.DaysSinceUnixEpoch(2000, 1, 1);
        // Second==60 -> end of day: same count as next midnight (sod = 86400).
        long sod = Second == 60
            ? 86400L
            : Hour * 3600L + Minute * 60L + Second;
        return days * 86400.0 + (sod - 43200.0) + Fraction;
    }

    /// <summary>Canonical minimal rendering (fraction digits trimmed).</summary>
    public string ToCanonicalString() => ToFixedString(-1);

    /// <summary>
    /// Render with exactly <paramref name="decimals"/> fraction digits
    /// (0-9), or minimal digits when negative.
    /// </summary>
    public string ToFixedString(int decimals)
    {
        string frac = "";
        if (decimals >= 0)
        {
            if (decimals > 0)
            {
                long scaled = (long)Math.Round(Fraction * Math.Pow(10, decimals), MidpointRounding.AwayFromZero);
                if (scaled >= (long)Math.Pow(10, decimals))
                    scaled = (long)Math.Pow(10, decimals) - 1; // guard: never roll into the next second here
                frac = "." + scaled.ToString().PadLeft(decimals, '0');
            }
        }
        else if (Fraction > 0.0)
        {
            // Minimal digits, round-trip safe to 9 decimals.
            long scaled = (long)Math.Round(Fraction * 1e9, MidpointRounding.AwayFromZero);
            frac = "." + scaled.ToString("D9").TrimEnd('0');
        }
        return $"{Year:D4}-{Month:D2}-{Day:D2}T{Hour:D2}:{Minute:D2}:{Second:D2}{frac}Z";
    }

    public bool Equals(UtcInstant other) =>
        Year == other.Year && Month == other.Month && Day == other.Day &&
        Hour == other.Hour && Minute == other.Minute && Second == other.Second &&
        Fraction.Equals(other.Fraction);

    public override bool Equals(object? obj) => obj is UtcInstant other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(Year, Month, Day, Hour, Minute, Second, Fraction);

    public int CompareTo(UtcInstant other)
    {
        int c = Year.CompareTo(other.Year);
        if (c != 0) return c;
        c = Month.CompareTo(other.Month);
        if (c != 0) return c;
        c = Day.CompareTo(other.Day);
        if (c != 0) return c;
        c = Hour.CompareTo(other.Hour);
        if (c != 0) return c;
        c = Minute.CompareTo(other.Minute);
        if (c != 0) return c;
        c = Second.CompareTo(other.Second);
        if (c != 0) return c;
        return Fraction.CompareTo(other.Fraction);
    }

    public override string ToString() => ToCanonicalString();

    private static bool TryDigits(string text, int start, int count, out int value)
    {
        value = 0;
        if (start + count > text.Length)
            return false;
        for (int i = 0; i < count; i++)
        {
            char c = text[start + i];
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + (c - '0');
        }
        return true;
    }

    private static string Quote(string? text) => text is null ? "<null>" : $"'{text}'";
}
