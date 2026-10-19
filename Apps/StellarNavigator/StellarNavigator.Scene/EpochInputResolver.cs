using System.Globalization;
using Astronomy.Time;

namespace StellarNavigator.Scene;

/// <summary>Resolves explicit ET, JD TDB, or UTC text using only the SN5 converter.</summary>
public sealed class EpochInputResolver(TimeConverter? converter)
{
    private readonly TimeConverter? _converter = converter;

    public static EtSeconds StepEt(EtSeconds current, double seconds)
    {
        if (!double.IsFinite(current.Value) || !double.IsFinite(seconds))
            throw new ArgumentOutOfRangeException(nameof(seconds), "ET and step must be finite.");
        double next = current.Value + seconds;
        if (!double.IsFinite(next))
            throw new ArgumentOutOfRangeException(nameof(seconds), "ET step overflowed the finite epoch range.");
        return new EtSeconds(next);
    }

    public EpochResolution Resolve(EpochInputKind kind, string? text, bool allowProvisionalUtc = false)
    {
        if (string.IsNullOrWhiteSpace(text))
            return EpochResolution.Failed(kind, "Enter an epoch value.");

        switch (kind)
        {
            case EpochInputKind.EtSeconds:
                if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double et) ||
                    !double.IsFinite(et))
                    return EpochResolution.Failed(kind, "ET must be a finite number of TDB seconds past J2000.");
                return new(true, new EtSeconds(et), kind, null,
                    TimeRepresentationPrecision.EtSeconds,
                    "Direct ET input; no UTC leap-second conversion was used.", null);

            case EpochInputKind.JulianDateTdb:
                if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double jd) ||
                    !double.IsFinite(jd))
                    return EpochResolution.Failed(kind, "JD TDB must be a finite Julian date.");
                return new(true, EtSeconds.FromJulianDateTdb(jd), kind, null,
                    TimeRepresentationPrecision.JulianDateDouble,
                    "JD TDB input converted to ET; binary64 JD limits epoch resolution to tens of microseconds near modern dates.", null);

            case EpochInputKind.Utc:
                if (_converter is null)
                    return EpochResolution.Failed(kind,
                        "UTC conversion is unavailable because the authenticated SN5 leap-second table did not load.",
                        TimeConversionQuality.TimeTableInvalid);
                if (!_converter.TryUtcToEt(text, out var result, out var error, allowProvisionalUtc))
                    return EpochResolution.Failed(kind,
                        $"UTC conversion refused [{error!.Quality}]: {error.Message}", error.Quality);
                var utc = result!;
                string provenance = utc.Quality == TimeConversionQuality.ProvisionalStaleTable
                    ? $"UTC converted provisionally using the last announced leap-second offset; {utc.LeapSecondKernelIdentity}."
                    : $"UTC converted using authenticated SN5 data; {utc.LeapSecondKernelIdentity}.";
                return new(true, utc.Et, kind, utc.Quality,
                    TimeRepresentationPrecision.EtSeconds, provenance, null);

            default:
                return EpochResolution.Failed(kind, "Unsupported time input mode.");
        }
    }
}
