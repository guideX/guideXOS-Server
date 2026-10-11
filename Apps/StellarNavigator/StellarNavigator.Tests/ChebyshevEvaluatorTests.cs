using Astronomy.Ephemeris;
using Xunit;

namespace StellarNavigator.Tests;

/// <summary>
/// Unit tests for the pure-managed Chebyshev evaluator (no dataset needed).
/// Reference values are analytic (T_0, T_1, T_2, naive summation), never the
/// provider under test.
/// </summary>
public class ChebyshevEvaluatorTests
{
    [Fact]
    public void Evaluate_ConstantPolynomial_ReturnsC0()
    {
        double[] c = { 2.5 };
        foreach (var u in new[] { -1.0, -0.3, 0.0, 0.7, 1.0 })
            Assert.Equal(2.5, ChebyshevEvaluator.Evaluate(c, u), 15);
    }

    [Fact]
    public void Evaluate_T1_ReturnsU()
    {
        double[] c = { 0.0, 1.0 };
        foreach (var u in new[] { -1.0, -0.25, 0.0, 0.5, 1.0 })
            Assert.Equal(u, ChebyshevEvaluator.Evaluate(c, u), 14);
    }

    [Fact]
    public void Evaluate_T2_ReturnsTwoUSquaredMinusOne()
    {
        double[] c = { 0.0, 0.0, 1.0 };
        foreach (var u in new[] { -1.0, -0.5, 0.0, 0.25, 1.0 })
            Assert.Equal(2 * u * u - 1.0, ChebyshevEvaluator.Evaluate(c, u), 13);
    }

    [Fact]
    public void Evaluate_MatchesNaiveSummation()
    {
        var rng = new Random(20261011);
        for (int trial = 0; trial < 25; trial++)
        {
            int degree = rng.Next(1, 14);
            var c = new double[degree + 1];
            for (int i = 0; i < c.Length; i++)
                c[i] = (rng.NextDouble() - 0.5) * 2e8;
            for (int k = 0; k <= 10; k++)
            {
                double u = -1.0 + 2.0 * k / 10.0;
                double expected = Naive(c, u);
                double actual = ChebyshevEvaluator.Evaluate(c, u);
                double tol = 1e-9 * Math.Max(1.0, Math.Abs(expected));
                Assert.True(Math.Abs(expected - actual) <= tol,
                    $"trial {trial} u={u}: expected {expected:R}, actual {actual:R}");
            }
        }
    }

    [Fact]
    public void Derivative_T2_ReturnsFourU()
    {
        double[] c = { 0.0, 0.0, 1.0 };
        foreach (var u in new[] { -1.0, -0.5, 0.0, 0.25, 1.0 })
        {
            var (_, d) = ChebyshevEvaluator.EvaluateWithDerivative(c, u);
            Assert.Equal(4 * u, d, 12);
        }
    }

    [Fact]
    public void Derivative_MatchesFiniteDifference()
    {
        var rng = new Random(42);
        var c = new double[11];
        for (int i = 0; i < c.Length; i++)
            c[i] = (rng.NextDouble() - 0.5) * 1e7;
        foreach (var u in new[] { -0.9, -0.31, 0.0, 0.0625, 0.77, 0.999 })
        {
            var (v, d) = ChebyshevEvaluator.EvaluateWithDerivative(c, u);
            double expected = Naive(c, u);
            Assert.True(Math.Abs(expected - v) <= 1e-9 * Math.Max(1.0, Math.Abs(expected)),
                $"u={u}: expected {expected:R}, actual {v:R}");
            double h = 1e-7;
            double fd = (ChebyshevEvaluator.Evaluate(c, u + h) - ChebyshevEvaluator.Evaluate(c, u - h)) / (2 * h);
            Assert.True(Math.Abs(fd - d) <= 1e-6 * Math.Max(1.0, Math.Abs(d)),
                $"u={u}: finite-difference {fd:R} vs analytic {d:R}");
        }
    }

    [Fact]
    public void Derivative_Constant_IsZero()
    {
        double[] c = { -123.456 };
        var (v, d) = ChebyshevEvaluator.EvaluateWithDerivative(c, 0.3);
        Assert.Equal(-123.456, v, 15);
        Assert.Equal(0.0, d, 15);
    }

    [Fact]
    public void NormalizeTime_FollowsMidpointRadiusConvention()
    {
        Assert.Equal(0.00625, ChebyshevEvaluator.NormalizeTime(-43200.0 + 4320.0, -43200.0, 691200.0), 15);
        Assert.Equal(-1.0, ChebyshevEvaluator.NormalizeTime(-100.0, 0.0, 100.0), 15);
        Assert.Equal(1.0, ChebyshevEvaluator.NormalizeTime(100.0, 0.0, 100.0), 15);
    }

    [Fact]
    public void NormalizeTime_RejectsNonPositiveRadius()
    {
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.NormalizeTime(0.0, 0.0, 0.0));
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.NormalizeTime(0.0, 0.0, -5.0));
    }

    [Fact]
    public void Evaluator_RejectsInvalidInputs()
    {
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.Evaluate(Array.Empty<double>(), 0.0));
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.Evaluate(new[] { 1.0, double.NaN }, 0.0));
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.Evaluate(new[] { 1.0 }, double.PositiveInfinity));
        Assert.Throws<ArgumentException>(() => ChebyshevEvaluator.EvaluateWithDerivative(new[] { 1.0, double.PositiveInfinity }, 0.0));
    }

    private static double Naive(double[] c, double u)
    {
        double t0 = 1.0, t1 = u;
        double s = c[0] * t0 + (c.Length > 1 ? c[1] * t1 : 0.0);
        for (int n = 2; n < c.Length; n++)
        {
            double t2 = 2 * u * t1 - t0;
            s += c[n] * t2;
            t0 = t1;
            t1 = t2;
        }
        return s;
    }
}
