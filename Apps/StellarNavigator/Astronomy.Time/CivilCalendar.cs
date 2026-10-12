namespace Astronomy.Time;

/// <summary>
/// Proleptic-Gregorian civil calendar arithmetic (Howard Hinnant algorithms),
/// independent of the OS locale, timezone database, or platform DateTime leap
/// handling. All Stellar Navigator UTC field conversions go through here so
/// historical and far-future dates behave deterministically.
/// </summary>
internal static class CivilCalendar
{
    // Days since 1970-01-01 (Unix epoch). Exact for all realistic years.
    public static long DaysSinceUnixEpoch(int year, int month, int day)
    {
        int y = year;
        int m = month;
        if (m <= 2)
        {
            y -= 1;
            m += 12;
        }
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;                                   // [0, 399]
        long doy = (153 * (m - 3) + 2) / 5 + day - 1;               // [0, 365]
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
        return era * 146097 + doe - 719468;
    }

    public static void CivilFromDaysSinceUnixEpoch(long days, out int year, out int month, out int day)
    {
        long z = days + 719468;
        long era = (z >= 0 ? z : z - 146096) / 146097;
        long doe = z - era * 146097;                                // [0, 146096]
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long y = yoe + era * 400;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        day = (int)(doy - (153 * mp + 2) / 5 + 1);
        month = (int)(mp < 10 ? mp + 3 : mp - 9);
        year = (int)(month <= 2 ? y + 1 : y);
    }

    public static bool IsLeapYear(int year) =>
        year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);

    public static int DaysInMonth(int year, int month) => month switch
    {
        1 or 3 or 5 or 7 or 8 or 10 or 12 => 31,
        4 or 6 or 9 or 11 => 30,
        2 => IsLeapYear(year) ? 29 : 28,
        _ => 0,
    };
}
