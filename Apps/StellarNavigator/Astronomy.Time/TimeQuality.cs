namespace Astronomy.Time;

/// <summary>
/// Quality of a time-scale conversion. Ephemeris-source quality
/// (NavigationResultStatus) is reported separately and never conflated
/// with this: a physically accurate polynomial evaluation at the wrong
/// epoch is still a wrong navigation result.
/// </summary>
public enum TimeConversionQuality
{
    /// <summary>Authenticated table, inside the announced-data interval.</summary>
    Validated,
    /// <summary>
    /// Explicit opt-in computation past the last announced leap second using
    /// the last recorded offset (CSPICE-compatible assumption, NOT knowledge).
    /// </summary>
    ProvisionalStaleTable,
    /// <summary>JD-double API path: correct epoch, but ~80 us quantization flagged.</summary>
    PrecisionLimited,
    /// <summary>UTC before 1972-01-01: no authoritative leap-second data.</summary>
    UnsupportedHistorical,
    /// <summary>UTC past the last announced transition without provisional opt-in.</summary>
    UnsupportedFuture,
    /// <summary>Malformed UTC text or an unsupported :60 leap-second label.</summary>
    InvalidUtcInput,
    /// <summary>Non-finite ET input.</summary>
    InvalidEtInput,
    /// <summary>Time-data asset missing, corrupt, or failed integrity check.</summary>
    TimeTableInvalid,
}

/// <summary>ET seconds past J2000 on the TDB scale (SPICE ET definition).</summary>
public readonly struct EtSeconds : IEquatable<EtSeconds>, IComparable<EtSeconds>
{
    public const double J2000JdTdb = 2451545.0;
    public const double SecondsPerDay = 86400.0;

    public double Value { get; }

    public EtSeconds(double value) { Value = value; }

    /// <summary>JD TDB. WARNING: a binary64 JD near 2.5e6 quantizes time to
    /// ~40-80 us; prefer <see cref="Value"/> for precision-sensitive work.</summary>
    public double ToJulianDateTdb() => J2000JdTdb + Value / SecondsPerDay;

    public static EtSeconds FromJulianDateTdb(double jdTdb) =>
        new((jdTdb - J2000JdTdb) * SecondsPerDay);

    public bool Equals(EtSeconds other) => Value.Equals(other.Value);
    public override bool Equals(object? obj) => obj is EtSeconds other && Equals(other);
    public override int GetHashCode() => Value.GetHashCode();
    public int CompareTo(EtSeconds other) => Value.CompareTo(other.Value);
    public override string ToString() => $"{Value:F6} s past J2000 (TDB)";
}

/// <summary>Successful UTC-to-ET conversion.</summary>
public sealed class UtcToEtResult
{
    public required UtcInstant Utc { get; init; }
    public required EtSeconds Et { get; init; }
    /// <summary>JD TDB convenience value (precision-limited; see Et).</summary>
    public required double JdTdb { get; init; }
    public required int TaiMinusUtcSeconds { get; init; }
    /// <summary>ET-UTC in seconds (DELTA_ET).</summary>
    public required double DeltaEtSeconds { get; init; }
    public required TimeConversionQuality Quality { get; init; }
    public required string LeapSecondKernelIdentity { get; init; }
    /// <summary>Upper error bound (representation + iteration) in seconds.</summary>
    public required double EstimatedErrorSeconds { get; init; }
}

/// <summary>Successful ET-to-UTC conversion.</summary>
public sealed class EtToUtcResult
{
    public required EtSeconds Et { get; init; }
    public required UtcInstant Utc { get; init; }
    public required int TaiMinusUtcSeconds { get; init; }
    public required TimeConversionQuality Quality { get; init; }
    public required string LeapSecondKernelIdentity { get; init; }
    public required double EstimatedErrorSeconds { get; init; }
}

/// <summary>Typed conversion failure (never an arbitrary date).</summary>
public sealed class TimeConversionError
{
    public required TimeConversionQuality Quality { get; init; }
    public required string Message { get; init; }
    public override string ToString() => $"[{Quality}] {Message}";
}
