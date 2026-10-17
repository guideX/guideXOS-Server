using Astronomy.Core;
using Astronomy.Time;
using Navigation.Core;

namespace Astronomy.Ephemeris;

/// <summary>
/// End-to-end UTC-to-ephemeris query: resolves a human-readable UTC timestamp
/// to ET via <see cref="TimeConverter"/>, then uses a precision-preserving
/// ET provider when available (with the legacy JD provider path retained for
/// compatibility). Time-conversion quality and
/// ephemeris-source quality are reported separately: a provisional (assumed,
/// CSPICE-compatible) UTC conversion downgrades an otherwise
/// <see cref="NavigationResultStatus.ValidatedReference"/> state to
/// <see cref="NavigationResultStatus.StaleData"/> so an assumed epoch is
/// never misrepresented as a verified navigation timestamp.
/// The stable Chebyshev evaluator and binary loader are untouched.
/// </summary>
public sealed class UtcEphemerisQuery
{
    private readonly IEphemerisProvider _provider;
    private readonly TimeConverter _converter;

    public UtcEphemerisQuery(IEphemerisProvider provider, TimeConverter converter)
    {
        _provider = provider ?? throw new ArgumentNullException(nameof(provider));
        _converter = converter ?? throw new ArgumentNullException(nameof(converter));
    }

    public sealed record ResolvedQuery
    {
        public required bool IsSuccess { get; init; }
        public required string SourceUtcText { get; init; }
        public UtcToEtResult? Time { get; init; }
        public TimeConversionError? TimeError { get; init; }
        public NavigationResult<(Vector3D Position, Vector3D Velocity)>? Ephemeris { get; init; }
        public required TimeConversionQuality TimeQuality { get; init; }
        public TimeRepresentationPrecision RepresentationPrecision { get; init; } = TimeRepresentationPrecision.EtSeconds;
        public NavigationResultStatus? EphemerisStatus => Ephemeris?.Status;
    }

    public ResolvedQuery GetState(string bodyId, string utcText, ReferenceFrame frame,
        bool allowProvisionalFuture = false)
    {
        if (!_converter.TryUtcToEt(utcText, out var time, out var timeError, allowProvisionalFuture))
        {
            return new ResolvedQuery
            {
                IsSuccess = false,
                SourceUtcText = utcText,
                TimeError = timeError,
                TimeQuality = timeError!.Quality,
                RepresentationPrecision = TimeRepresentationPrecision.EtSeconds,
            };
        }

        var t = time!;
        var epoch = new AstroTime(t.JdTdb, TimeScale.Tdb);
        bool usedEt = _provider is IEtEphemerisProvider;
        var state = usedEt
            ? ((IEtEphemerisProvider)_provider).GetStateAtEt(bodyId, t.Et, frame)
            : _provider.GetState(bodyId, epoch, frame);

        NavigationResult<(Vector3D Position, Vector3D Velocity)> reported = state;
        if (state.IsSuccess && t.Quality == TimeConversionQuality.ProvisionalStaleTable)
        {
            // Ephemeris evaluation is sound; the EPOCH is assumed. Downgrade.
            reported = NavigationResult<(Vector3D Position, Vector3D Velocity)>.Success(
                state.Value, NavigationResultStatus.StaleData, state.Epoch, state.Frame,
                state.Provenance + " | epoch provisional (stale leap-second table)");
        }

        return new ResolvedQuery
        {
            IsSuccess = reported.IsSuccess,
            SourceUtcText = utcText,
            Time = t,
            Ephemeris = reported,
            TimeQuality = t.Quality,
            RepresentationPrecision = usedEt
                ? TimeRepresentationPrecision.EtSeconds
                : TimeRepresentationPrecision.JulianDateDouble,
        };
    }
}
