using Astronomy.Core;
using Navigation.Core;

namespace Navigation.Core;

public static class SpacecraftPositioning
{
    public static NavigationResult<Vector3D> ComputeSpacecraftToPlanetVector(
        SpacecraftState spacecraft,
        NavigationResult<(Vector3D Position, Vector3D Velocity)> planetState)
    {
        if (!planetState.IsSuccess)
            return NavigationResult<Vector3D>.Failure(
                planetState.Status,
                $"Planet state unavailable: {planetState.ErrorMessage}");

        if (spacecraft.Frame.Kind != ReferenceFrameKind.Barycentric ||
            planetState.Frame?.Kind != ReferenceFrameKind.Barycentric)
            return NavigationResult<Vector3D>.Failure(
                NavigationResultStatus.InvalidInput,
                "Spacecraft-to-planet vector requires both states in barycentric frame");

        if (planetState.Epoch == null ||
            Math.Abs(spacecraft.Epoch.JdUtc - planetState.Epoch.Value.JdUtc) > 1e-10)
            return NavigationResult<Vector3D>.Failure(
                NavigationResultStatus.InvalidInput,
                "Cannot compute vector: mismatched epochs");

        var scToPlanet = planetState.Value!.Position - spacecraft.Position;
        return NavigationResult<Vector3D>.Success(
            scToPlanet,
            NavigationResultStatus.ApproximateOrSimulated,
            spacecraft.Epoch,
            ReferenceFrame.Barycentric,
            $"Spacecraft={spacecraft.Provenance}, Planet={planetState.Provenance}");
    }

    public static NavigationResult<double> ComputeSpacecraftToPlanetDistanceKm(
        SpacecraftState spacecraft,
        NavigationResult<(Vector3D Position, Vector3D Velocity)> planetState)
    {
        var vectorResult = ComputeSpacecraftToPlanetVector(spacecraft, planetState);
        if (!vectorResult.IsSuccess)
            return NavigationResult<double>.Failure(vectorResult.Status, vectorResult.ErrorMessage!);

        var distKm = vectorResult.Value!.Magnitude / Distance.MetersPerKilometer;
        return NavigationResult<double>.Success(
            distKm,
            NavigationResultStatus.ApproximateOrSimulated,
            spacecraft.Epoch,
            ReferenceFrame.Barycentric,
            vectorResult.Provenance);
    }
}
