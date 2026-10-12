using System.Text.Json;

namespace Astronomy.Time;

/// <summary>
/// Leap-second / DELTET parameter table derived from an official NAIF LSK.
///
/// Conventions (verified against CSPICE N0067 + naif0012.tls):
/// <list type="bullet">
/// <item>Uniform UTC seconds count 86400 s/day; a 23:59:60 label maps to the
/// same uniform count as the following midnight. The physical extra second
/// enters through the TAI-UTC offset step, exactly as SPICE DELTET does
/// (consecutive +1 s ET steps across :59 -&gt; :60 -&gt; :00).</item>
/// <item>Entry <c>(offset N, effective DATE)</c> takes effect at DATE 00:00:00
/// UTC for ordinary labels. A 23:59:60 label on the evening before DATE uses
/// the PREVIOUS offset (the step lands at midnight, not at the leap second).</item>
/// </list>
/// </summary>
public sealed class LeapSecondTable
{
    public sealed record Transition(double StartUniformSeconds, int Offset, int Year, int Month, int Day);

    public IReadOnlyList<Transition> Transitions { get; }
    public double DeltaTaSec { get; }
    public double K { get; }
    public double Eb { get; }
    public double M0Rad { get; }
    public double M1RadPerSec { get; }

    /// <summary>Uniform seconds of 1972-01-01T00:00:00 UTC (strict lower bound).</summary>
    public double StrictStartUtc => Transitions[0].StartUniformSeconds;
    /// <summary>Uniform seconds of the last transition midnight (strict upper bound).</summary>
    public double StrictEndUtc => Transitions[^1].StartUniformSeconds;

    public int FirstOffset => Transitions[0].Offset;
    public int LastOffset => Transitions[^1].Offset;

    internal LeapSecondTable(
        List<Transition> transitions,
        double deltaTaSec, double k, double eb, double m0, double m1)
    {
        Transitions = transitions.AsReadOnly();
        DeltaTaSec = deltaTaSec;
        K = k;
        Eb = eb;
        M0Rad = m0;
        M1RadPerSec = m1;
    }

    /// <summary>
    /// Build + validate from parsed asset fields. Throws InvalidDataException
    /// on unsorted/duplicated transitions, non-+1 offset steps, missing
    /// history anchor, or non-physical DELTET parameters.
    /// </summary>
    public static LeapSecondTable Create(
        IReadOnlyList<(string UtcMidnight, int Offset)> entries,
        double deltaTaSec, double k, double eb, double m0, double m1)
    {
        if (entries.Count < 2)
            throw new InvalidDataException("Leap-second table must hold at least two transitions.");
        if (!IsFinite(deltaTaSec) || !IsFinite(k) || !IsFinite(eb) || !IsFinite(m0) || !IsFinite(m1))
            throw new InvalidDataException("DELTET constants must be finite.");
        if (!(deltaTaSec > 0 && k > 0 && k < 0.01 && eb > 0 && eb < 0.05 && m1 > 0))
            throw new InvalidDataException("DELTET constants outside plausible physical range.");

        var dates = new List<(double Uniform, int Offset, int Y, int M, int D)>(entries.Count);
        foreach (var (text, offset) in entries)
        {
            if (!UtcInstant.TryParse(text, out var midnight, out _)
                || midnight.Hour != 0 || midnight.Minute != 0 || midnight.Second != 0 || midnight.Fraction != 0.0)
                throw new InvalidDataException($"Transition must be a UTC midnight: '{text}'.");
            double u = midnight.ToUniformSeconds();
            dates.Add((u, offset, midnight.Year, midnight.Month, midnight.Day));
        }

        var seen = new HashSet<double>();
        for (int i = 0; i < dates.Count; i++)
        {
            if (!seen.Add(dates[i].Uniform))
                throw new InvalidDataException("Duplicated leap-second transition.");
            if (i > 0 && dates[i].Uniform <= dates[i - 1].Uniform)
                throw new InvalidDataException("Leap-second transitions not strictly ordered.");
            if (i > 0 && dates[i].Offset != dates[i - 1].Offset + 1)
                throw new InvalidDataException(
                    $"TAI-UTC offset step must be exactly +1: {dates[i - 1].Offset} -> {dates[i].Offset}.");
        }
        if (dates[0].Y != 1972 || dates[0].M != 1 || dates[0].D != 1 || dates[0].Offset != 10)
            throw new InvalidDataException("First transition must anchor LSK history at 1972-01-01 offset 10.");
        if (dates[0].Offset < 1 || dates[0].Offset > 50)
            throw new InvalidDataException("Initial TAI-UTC offset outside plausible range.");

        var transitions = dates.Select(d => new Transition(d.Uniform, d.Offset, d.Y, d.M, d.D)).ToList();
        return new LeapSecondTable(transitions, deltaTaSec, k, eb, m0, m1);
    }

    /// <summary>
    /// TAI-UTC offset for an ordinary (non-:60) UTC label given as uniform
    /// seconds: offset of the last entry with start &lt;= uniform.
    /// Caller must have gated pre-1972 instants already.
    /// </summary>
    public int OffsetForUniform(double uniform)
    {
        int offset = Transitions[0].Offset;
        for (int i = 0; i < Transitions.Count; i++)
        {
            if (Transitions[i].StartUniformSeconds <= uniform)
                offset = Transitions[i].Offset;
            else
                break;
        }
        return offset;
    }

    /// <summary>
    /// True when (y,m,d) is the evening of a supported leap second, i.e. the
    /// day before some transition DATE other than the 1972-01-01 anchor
    /// (there was no leap second on 1971-12-31).
    /// </summary>
    public bool IsSupportedLeapDay(int year, int month, int day)
    {
        long probe = CivilCalendar.DaysSinceUnixEpoch(year, month, day);
        for (int i = 1; i < Transitions.Count; i++)
        {
            var t = Transitions[i];
            if (probe == CivilCalendar.DaysSinceUnixEpoch(t.Year, t.Month, t.Day) - 1)
                return true;
        }
        return false;
    }

    /// <summary>
    /// Uniform seconds of the leap second on (y,m,d) (== following midnight).
    /// Returns false when that evening holds no supported leap second.
    /// </summary>
    public bool TryGetLeapUniform(int year, int month, int day, out double uniform)
    {
        uniform = 0.0;
        if (!IsSupportedLeapDay(year, month, day))
            return false;
        var next = new UtcInstant(year, month, day, 23, 59, 60);
        uniform = next.ToUniformSeconds();
        return true;
    }

    /// <summary>
    /// When uniform lies in [S, S+1) for a leap transition start S (i.e. the
    /// label is either 23:59:60.f or 00:00:00.f), report both candidate offsets.
    /// <paramref name="tolerance"/> widens the window symmetrically to absorb
    /// binary64 representation noise in inverse conversions; callers
    /// forward-verify the candidates to disambiguate.
    /// </summary>
    public bool IsLeapBoundaryWindow(double uniform, out double windowStart, out int preOffset, out int postOffset,
        double tolerance = 0.0)
    {
        windowStart = 0.0;
        preOffset = postOffset = 0;
        for (int i = 1; i < Transitions.Count; i++)
        {
            double s = Transitions[i].StartUniformSeconds;
            if (uniform >= s - tolerance && uniform < s + 1.0 + tolerance)
            {
                windowStart = s;
                preOffset = Transitions[i - 1].Offset;
                postOffset = Transitions[i].Offset;
                return true;
            }
        }
        return false;
    }

    /// <summary>TAI-UTC offset just before a transition (for :60 labels).</summary>
    public int PreOffsetForLeap(int year, int month, int day)
    {
        long probe = CivilCalendar.DaysSinceUnixEpoch(year, month, day);
        for (int i = 1; i < Transitions.Count; i++)
        {
            var t = Transitions[i];
            if (probe == CivilCalendar.DaysSinceUnixEpoch(t.Year, t.Month, t.Day) - 1)
                return Transitions[i - 1].Offset;
        }
        throw new InvalidOperationException("Not a supported leap day.");
    }

    private static bool IsFinite(double v) => !double.IsNaN(v) && !double.IsInfinity(v);
}
