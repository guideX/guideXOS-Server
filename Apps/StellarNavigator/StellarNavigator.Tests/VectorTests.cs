using Astronomy.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class VectorTests
{
    [Fact]
    public void Magnitude_KnownVector()
    {
        var v = new Vector3D(3.0, 4.0, 0.0);
        Assert.Equal(5.0, v.Magnitude, 10);
    }

    [Fact]
    public void DistanceTo_KnownPoints()
    {
        var a = new Vector3D(0, 0, 0);
        var b = new Vector3D(1, 1, 1);
        Assert.Equal(Math.Sqrt(3), a.DistanceTo(b), 10);
    }

    [Fact]
    public void Normalized_UnitLength()
    {
        var v = new Vector3D(2.0, 3.0, 6.0);
        var n = v.Normalized();
        Assert.Equal(1.0, n.Magnitude, 10);
    }

    [Fact]
    public void Addition()
    {
        var a = new Vector3D(1, 2, 3);
        var b = new Vector3D(4, 5, 6);
        var sum = a + b;
        Assert.Equal(new Vector3D(5, 7, 9), sum);
    }

    [Fact]
    public void Subtraction()
    {
        var a = new Vector3D(4, 5, 6);
        var b = new Vector3D(1, 2, 3);
        var diff = a - b;
        Assert.Equal(new Vector3D(3, 3, 3), diff);
    }

    [Fact]
    public void ScalarMultiplication()
    {
        var v = new Vector3D(1, 2, 3);
        var scaled = v * 2.0;
        Assert.Equal(new Vector3D(2, 4, 6), scaled);
    }

    [Fact]
    public void ZeroVectorNormalization_Throws()
    {
        var v = new Vector3D(0, 0, 0);
        Assert.Throws<InvalidOperationException>(() => v.Normalized());
    }
}
