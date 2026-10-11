using Astronomy.Core;
using Navigation.Core;

namespace Astronomy.Ephemeris;

public sealed class RelativePositionResult
{
    public required string TargetBodyId { get; init; }
    public required string ObserverBodyId { get; init; }
    public required AstroTime Epoch { get; init; }
    public required Vector3D RelativePosition { get; init; }
    public required Vector3D RelativeVelocity { get; init; }
    public required double SeparationDistanceKm { get; init; }
    public required string DataSource { get; init; }
    public required NavigationResultStatus Status { get; init; }

    public override string ToString() =>
        $"{TargetBodyId} relative to {ObserverBodyId} at {Epoch}: " +
        $"pos=({RelativePosition.X / 1000.0:F1}, {RelativePosition.Y / 1000.0:F1}, {RelativePosition.Z / 1000.0:F1}) km, " +
        $"dist={SeparationDistanceKm:F1} km, src={DataSource}";
}

public sealed class RelativePositionCalculator
{
    private readonly IEphemerisProvider _provider;

    public RelativePositionCalculator(IEphemerisProvider provider)
    {
        _provider = provider;
    }

    public NavigationResult<RelativePositionResult> Calculate(
        string targetBodyId, string observerBodyId, AstroTime epoch)
    {
        var targetResult = _provider.GetState(targetBodyId, epoch, ReferenceFrame.Barycentric);
        if (!targetResult.IsSuccess)
            return NavigationResult<RelativePositionResult>.Failure(
                targetResult.Status,
                $"Target state unavailable: {targetResult.ErrorMessage}");

        var observerResult = _provider.GetState(observerBodyId, epoch, ReferenceFrame.Barycentric);
        if (!observerResult.IsSuccess)
            return NavigationResult<RelativePositionResult>.Failure(
                observerResult.Status,
                $"Observer state unavailable: {observerResult.ErrorMessage}");

        var (targetPos, targetVel) = targetResult.Value!;
        var (observerPos, observerVel) = observerResult.Value!;

        if (targetResult.Epoch == null || observerResult.Epoch == null ||
            Math.Abs(targetResult.Epoch.Value.JdUtc - observerResult.Epoch.Value.JdUtc) > 1e-10)
            return NavigationResult<RelativePositionResult>.Failure(
                NavigationResultStatus.InvalidInput,
                "Cannot compute relative position: mismatched epochs");

        var relPos = targetPos - observerPos;
        var relVel = targetVel - observerVel;
        var distKm = relPos.Magnitude / Distance.MetersPerKilometer;

        return NavigationResult<RelativePositionResult>.Success(
            new RelativePositionResult
            {
                TargetBodyId = targetBodyId,
                ObserverBodyId = observerBodyId,
                Epoch = epoch,
                RelativePosition = relPos,
                RelativeVelocity = relVel,
                SeparationDistanceKm = distKm,
                DataSource = _provider.DataSource,
                Status = NavigationResultStatus.ValidatedReference
            },
            NavigationResultStatus.ValidatedReference,
            epoch,
            ReferenceFrame.Barycentric,
            _provider.DataSource);
    }
}
