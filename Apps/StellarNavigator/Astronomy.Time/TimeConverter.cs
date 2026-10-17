namespace Astronomy.Time;

/// <summary>
/// Scientifically justified UTC/TAI/TT/TDB conversion chain over the
/// authenticated LSK-derived table:
///
/// <c>UTC --(TAI-UTC leap offset)--> TAI --(+32.184 s)--> TT --(periodic TDB-TT)--> TDB(ET)</c>
///
/// Mathematics (LSK-compatible Moyer formulation, same as CSPICE DELTET):
/// <code>
/// ET = U + DTA + 32.184 + K * sin(E)      (seconds)
/// E  = M + EB * sin(M),   M = M0 + M1 * ET
/// </code>
/// where U is uniform UTC seconds (86400 s/day; a 23:59:60 label shares its
/// count with the following midnight) and DTA is the label's TAI-UTC offset.
/// The equation is implicit in ET (contraction factor K*M1 ~ 3.3e-10) and is
/// solved by fixed-point iteration to 1e-13 s (2-3 passes).
///
/// Policy (never silent):
/// <list type="bullet">
/// <item>UTC before 1972-01-01T00:00:00Z: refused (<c>UnsupportedHistorical</c>)
/// in every mode. CSPICE would extrapolate with the initial offset; that is
/// an approximation, not knowledge.</item>
/// <item>UTC after the last announced transition (2017-01-01T00:00:00Z):
/// refused (<c>UnsupportedFuture</c>) unless the caller explicitly opts into
/// provisional mode, which reuses the last recorded offset exactly like
/// CSPICE and reports <c>ProvisionalStaleTable</c>.</item>
/// <item>An unsupported 23:59:60 label is a typed failure, never normalized.</item>
/// </list>
/// Thread-safe and allocation-light after table load.
/// </summary>
public sealed class TimeConverter
{
    public const double TtMinusTaiSeconds = 32.184;
    private const double ForwardToleranceSeconds = 1e-13;
    private const int MaxIterations = 20;
    /// <summary>Snap threshold for inverse second fractions (representation noise).</summary>
    private const double InverseSnapSeconds = 1e-6;

    private readonly LeapSecondTable _table;
    private readonly string _kernelIdentity;

    public TimeConverter(LeapSecondTable table, string kernelIdentity)
    {
        _table = table;
        _kernelIdentity = kernelIdentity;
    }

    public LeapSecondTable Table => _table;

    /// <summary>TDB-TT periodic term (s) evaluated directly at ET.</summary>
    public double PeriodicTdbMinusTt(double etSeconds)
    {
        double m = _table.M0Rad + _table.M1RadPerSec * etSeconds;
        double e = m + _table.Eb * Math.Sin(m);
        return _table.K * Math.Sin(e);
    }

    // ---------- UTC -> ET ----------

    public bool TryUtcToEt(string? utcText, out UtcToEtResult? result, out TimeConversionError? error,
        bool allowProvisionalFuture = false)
    {
        result = null;
        if (!UtcInstant.TryParse(utcText, out var utc, out string? parseError))
        {
            error = new TimeConversionError { Quality = TimeConversionQuality.InvalidUtcInput, Message = parseError! };
            return false;
        }
        return TryUtcToEt(utc, out result, out error, allowProvisionalFuture);
    }

    public bool TryUtcToEt(UtcInstant utc, out UtcToEtResult? result, out TimeConversionError? error,
        bool allowProvisionalFuture = false)
    {
        result = null;
        error = null;
        if (!utc.TryValidate(out string? validationError))
        {
            error = new TimeConversionError
            {
                Quality = TimeConversionQuality.InvalidUtcInput,
                Message = validationError!,
            };
            return false;
        }
        double uniform = utc.ToUniformSeconds();
        int dta;

        if (utc.IsLeapSecond)
        {
            if (!_table.IsSupportedLeapDay(utc.Year, utc.Month, utc.Day))
            {
                error = new TimeConversionError
                {
                    Quality = TimeConversionQuality.InvalidUtcInput,
                    Message = $"Unsupported leap second '{utc.ToCanonicalString()}': no leap second announced for that date (rejected, not normalized).",
                };
                return false;
            }
            dta = _table.PreOffsetForLeap(utc.Year, utc.Month, utc.Day);
        }
        else
        {
            dta = _table.OffsetForUniform(uniform);
        }

        if (uniform < _table.StrictStartUtc)
        {
            error = new TimeConversionError
            {
                Quality = TimeConversionQuality.UnsupportedHistorical,
                Message = $"UTC '{utc.ToCanonicalString()}' predates 1972-01-01: no authoritative leap-second data (pre-1972 UTC conventions unsupported).",
            };
            return false;
        }

        var quality = TimeConversionQuality.Validated;
        // Leap-second labels are announced table data, so they are always
        // strict (their uniform count lies in [S_i, S_i+1)). Only ordinary
        // labels past the last transition midnight assume an unannounced
        // future offset.
        if (!utc.IsLeapSecond && uniform > _table.StrictEndUtc)
        {
            if (!allowProvisionalFuture)
            {
                error = new TimeConversionError
                {
                    Quality = TimeConversionQuality.UnsupportedFuture,
                    Message = $"UTC '{utc.ToCanonicalString()}' is past the last announced leap-second transition " +
                        $"({_table.Transitions[^1].Year:D4}-{_table.Transitions[^1].Month:D2}-{_table.Transitions[^1].Day:D2}); " +
                        "no future leap seconds are assumed in strict mode.",
                };
                return false;
            }
            quality = TimeConversionQuality.ProvisionalStaleTable;
        }

        // TAI stage (s past the TAI instant of 2000-01-01T12:00:00 UTC), then TT stage.
        double ttMinusRef = uniform + dta + TtMinusTaiSeconds;
        double et = ttMinusRef; // seed ignores the <= 1.7 ms periodic term
        for (int i = 0; i < MaxIterations; i++)
        {
            double next = ttMinusRef + PeriodicTdbMinusTt(et);
            if (Math.Abs(next - et) <= ForwardToleranceSeconds)
            {
                et = next;
                break;
            }
            et = next;
            if (i == MaxIterations - 1)
            {
                error = new TimeConversionError
                {
                    Quality = TimeConversionQuality.TimeTableInvalid,
                    Message = "TT->TDB iteration failed to converge (table parameters suspect).",
                };
                return false;
            }
        }

        double bound = Math.Max(ForwardToleranceSeconds * 10.0, Math.Abs(et) * 2.3e-16 * 4.0);
        result = new UtcToEtResult
        {
            Utc = utc,
            Et = new EtSeconds(et),
            JdTdb = EtSeconds.J2000JdTdb + et / EtSeconds.SecondsPerDay,
            TaiMinusUtcSeconds = dta,
            DeltaEtSeconds = et - uniform,
            Quality = quality,
            LeapSecondKernelIdentity = _kernelIdentity,
            EstimatedErrorSeconds = bound,
        };
        return true;
    }

    // ---------- ET -> UTC ----------

    public bool TryEtToUtc(double etSeconds, out EtToUtcResult? result, out TimeConversionError? error,
        bool allowProvisionalFuture = false)
    {
        result = null;
        error = null;
        if (double.IsNaN(etSeconds) || double.IsInfinity(etSeconds))
        {
            error = new TimeConversionError
            {
                Quality = TimeConversionQuality.InvalidEtInput,
                Message = "ET must be a finite number of seconds past J2000 (TDB).",
            };
            return false;
        }

        double periodic = PeriodicTdbMinusTt(etSeconds);

        // Resolve the label-dependent TAI-UTC offset by fixed-point iteration.
        // Near a transition the iteration 2-cycles between (pre, S) and
        // (post, S-1): neither is a fixed point of the date lookup because
        // the true solution is a leap-SECOND label. Candidate verification
        // below resolves every landing within ~1 s, so iteration precision
        // beyond that is unnecessary.
        int dta = _table.LastOffset;
        double uniform = etSeconds - dta - TtMinusTaiSeconds - periodic;
        for (int i = 0; i < 8; i++)
        {
            int next = _table.OffsetForUniform(uniform);
            double nextUniform = etSeconds - next - TtMinusTaiSeconds - periodic;
            if (next == dta && nextUniform == uniform)
                break;
            dta = next;
            uniform = nextUniform;
        }

        if (uniform < _table.StrictStartUtc - 2.0 &&
            !_table.IsLeapBoundaryWindow(uniform, out _, out _, out _) &&
            !_table.IsLeapBoundaryWindow(uniform + 1.0, out _, out _, out _))
        {
            error = new TimeConversionError
            {
                Quality = TimeConversionQuality.UnsupportedHistorical,
                Message = $"ET {etSeconds:F6} s predates 1972-01-01 UTC: no authoritative UTC label.",
            };
            return false;
        }

        // Tolerance absorbs binary64 noise (~3e-7 s at modern magnitudes).
        // Forward branches sharing one count are 1 s apart, so verification
        // is unambiguous; exact ties keep the ordinary label because only a
        // strictly-better leap reproduction replaces it.
        const double WindowTolerance = 2e-6;
        UtcInstant bestLabel = UniformToUtc(uniform);
        int bestOffset = _table.OffsetForUniform(uniform);
        double bestErr = Math.Abs(uniform + bestOffset + TtMinusTaiSeconds + periodic - etSeconds);
        bool bestIsLeap = false;

        void ConsiderLeap(double windowStart, double frac, int preOffset)
        {
            if (frac < 0.0 || frac > 1.0)
                return;
            double err = Math.Abs(windowStart + frac + preOffset + TtMinusTaiSeconds + periodic - etSeconds);
            if (err < bestErr)
            {
                CivilCalendar.CivilFromDaysSinceUnixEpoch(
                    LeapDayNumber(windowStart), out int y, out int m, out int d);
                bestLabel = new UtcInstant(y, m, d, 23, 59, 60, SnapFraction(frac));
                bestOffset = preOffset;
                bestErr = err;
                bestIsLeap = true;
            }
        }

        if (_table.IsLeapBoundaryWindow(uniform, out double ws0, out int pre0, out _, WindowTolerance))
            ConsiderLeap(ws0, Math.Max(0.0, uniform - ws0), pre0);
        if (_table.IsLeapBoundaryWindow(uniform + 1.0, out double ws1, out int pre1, out _, WindowTolerance))
            ConsiderLeap(ws1, uniform + 1.0 - ws1, pre1);

        UtcInstant utc = bestLabel;
        int usedDta = bestOffset;
        bool isLeapLabel = bestIsLeap;

        var quality = TimeConversionQuality.Validated;
        if (!isLeapLabel && uniform > _table.StrictEndUtc)
        {
            if (!allowProvisionalFuture)
            {
                error = new TimeConversionError
                {
                    Quality = TimeConversionQuality.UnsupportedFuture,
                    Message = $"ET {etSeconds:F6} s maps past the last announced leap-second transition; " +
                        "no future leap seconds are assumed in strict mode.",
                };
                return false;
            }
            quality = TimeConversionQuality.ProvisionalStaleTable;
        }

        double bound = Math.Max(ForwardToleranceSeconds * 10.0, Math.Abs(etSeconds) * 2.3e-16 * 4.0);
        result = new EtToUtcResult
        {
            Et = new EtSeconds(etSeconds),
            Utc = utc,
            TaiMinusUtcSeconds = usedDta,
            Quality = quality,
            LeapSecondKernelIdentity = _kernelIdentity,
            EstimatedErrorSeconds = bound,
        };
        return true;
    }

    /// <summary>
    /// JD-TDB convenience path. The binary64 JD quantizes time to ~40-80 us
    /// near 2.5e6. Time-data trust remains in the result quality, while this
    /// representation limitation is reported independently.
    /// </summary>
    public bool TryUtcToJdTdb(string? utcText, out UtcToEtResult? result, out TimeConversionError? error,
        bool allowProvisionalFuture = false)
    {
        if (!TryUtcToEt(utcText, out result, out error, allowProvisionalFuture))
            return false;
        var ok = result!;
        result = new UtcToEtResult
        {
            Utc = ok.Utc,
            Et = ok.Et,
            JdTdb = ok.JdTdb,
            TaiMinusUtcSeconds = ok.TaiMinusUtcSeconds,
            DeltaEtSeconds = ok.DeltaEtSeconds,
            Quality = ok.Quality,
            RepresentationPrecision = TimeRepresentationPrecision.JulianDateDouble,
            LeapSecondKernelIdentity = ok.LeapSecondKernelIdentity,
            EstimatedErrorSeconds = Math.Max(ok.EstimatedErrorSeconds, 8.0e-5),
        };
        return true;
    }

    // ---------- helpers ----------

    private static UtcInstant UniformToUtc(double uniform)
    {
        long dayIndex = (long)Math.Floor((uniform + 43200.0) / 86400.0);
        long absDay = CivilCalendar.DaysSinceUnixEpoch(2000, 1, 1) + dayIndex;
        CivilCalendar.CivilFromDaysSinceUnixEpoch(absDay, out int y, out int m, out int d);
        double sod = uniform - (dayIndex * 86400.0 - 43200.0);
        // Snap representation noise at exact seconds (never disturbs real fractions >= 1 us).
        double rounded = Math.Round(sod);
        if (Math.Abs(sod - rounded) < InverseSnapSeconds)
            sod = rounded;
        if (sod < 0)
        {
            absDay -= 1;
            CivilCalendar.CivilFromDaysSinceUnixEpoch(absDay, out y, out m, out d);
            sod += 86400.0;
        }
        else if (sod >= 86400.0)
        {
            absDay += 1;
            CivilCalendar.CivilFromDaysSinceUnixEpoch(absDay, out y, out m, out d);
            sod -= 86400.0;
        }
        int hour = (int)(sod / 3600.0);
        int minute = (int)((sod - hour * 3600.0) / 60.0);
        double secF = sod - hour * 3600.0 - minute * 60.0;
        int second = (int)Math.Floor(secF);
        double fraction = SnapFraction(secF - second);
        if (second >= 60)
        {
            // Uniform counts never produce :60 outside the leap window (handled by caller).
            second = 59;
            fraction = 0.999999999;
        }
        return new UtcInstant(y, m, d, hour, minute, second, fraction);
    }

    private static double SnapFraction(double f)
    {
        if (f < 0) return 0.0;
        if (f < InverseSnapSeconds || 1.0 - f < InverseSnapSeconds) return 0.0;
        return f;
    }

    private static long LeapDayNumber(double windowStart)
    {
        // windowStart is a transition midnight; the leap evening is the day before.
        long dayIndex = (long)Math.Floor((windowStart + 43200.0) / 86400.0);
        return CivilCalendar.DaysSinceUnixEpoch(2000, 1, 1) + dayIndex - 1;
    }
}
