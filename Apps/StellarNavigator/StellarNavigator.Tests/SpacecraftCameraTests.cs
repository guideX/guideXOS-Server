using Astronomy.Core;
using Navigation.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class SpacecraftCameraTests
{
    [Fact]
    public void SpacecraftState_IndependentOfCamera()
    {
        var scState = new SpacecraftState
        {
            SpacecraftId = "sim",
            Position = new Vector3D(1e9, 2e9, 3e9),
            Velocity = new Vector3D(1000, 2000, 3000),
            Frame = ReferenceFrame.Heliocentric,
            CenterOfReferenceId = "sun",
            Epoch = AstroTime.FromJ2000Days(0),
            Provenance = StateProvenance.Simulated
        };

        var cameraPosition = new Vector3D(0, 0, 0);

        Assert.NotEqual(scState.Position, cameraPosition);
        Assert.Equal(new Vector3D(1e9, 2e9, 3e9), scState.Position);
    }

    [Fact]
    public void ObserverState_DirectionTo()
    {
        var observer = new ObserverState
        {
            ObserverId = "obs",
            Position = new Vector3D(0, 0, 0),
            Frame = ReferenceFrame.IcrfJ2000,
            CenterOfReferenceId = "ssb",
            Epoch = AstroTime.FromJ2000Days(0)
        };

        var target = new Vector3D(1, 0, 0);
        var dir = observer.DirectionTo(target);
        Assert.Equal(1.0, dir.X, 10);
        Assert.Equal(0.0, dir.Y, 10);
        Assert.Equal(0.0, dir.Z, 10);
    }

    [Fact]
    public void ObserverState_DistanceTo()
    {
        var observer = new ObserverState
        {
            ObserverId = "obs",
            Position = new Vector3D(0, 0, 0),
            Frame = ReferenceFrame.IcrfJ2000,
            CenterOfReferenceId = "ssb",
            Epoch = AstroTime.FromJ2000Days(0)
        };

        var target = new Vector3D(3, 4, 0);
        Assert.Equal(5.0, observer.DistanceTo(target), 10);
    }

    [Fact]
    public void SpacecraftState_ProvenanceIsSimulated()
    {
        var scState = new SpacecraftState
        {
            SpacecraftId = "sim",
            Position = new Vector3D(0, 0, 0),
            Velocity = new Vector3D(0, 0, 0),
            Frame = ReferenceFrame.Heliocentric,
            CenterOfReferenceId = "sun",
            Epoch = AstroTime.FromJ2000Days(0),
            Provenance = StateProvenance.Simulated
        };

        Assert.Equal(StateProvenance.Simulated, scState.Provenance);
        Assert.Equal(NavigationResultStatus.ApproximateOrSimulated, scState.Quality);
    }

    [Fact]
    public void NavigationResult_Success()
    {
        var result = NavigationResult<double>.Success(42.0, NavigationResultStatus.ValidatedReference);
        Assert.True(result.IsSuccess);
        Assert.Equal(42.0, result.Value);
    }

    [Fact]
    public void NavigationResult_Failure()
    {
        var result = NavigationResult<double>.Failure(NavigationResultStatus.InvalidInput, "bad input");
        Assert.False(result.IsSuccess);
        Assert.Equal("bad input", result.ErrorMessage);
    }
}
