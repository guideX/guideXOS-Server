namespace Astronomy.Ephemeris;

/// <summary>
/// Pure-managed evaluation of Chebyshev polynomial segments (SPK Type 2).
/// No UI, I/O, or native dependencies. All math is double precision.
///
/// A Type 2 record stores position-only coefficients:
/// value(u) = sum_{n=0..PD} c_n * T_n(u), with u = (t - midpoint) / radius
/// in [-1, 1]. Velocity is the time derivative of the same polynomial with
/// chain-rule scaling 1/radius (derivative w.r.t. normalized coordinate
/// must NOT be confused with derivative w.r.t. physical time).
/// </summary>
public static class ChebyshevEvaluator
{
    /// <summary>
    /// Normalizes an ephemeris time into a record's Chebyshev domain.
    /// </summary>
    public static double NormalizeTime(double epochSeconds, double midpointSeconds, double radiusSeconds)
    {
        if (!double.IsFinite(epochSeconds) || !double.IsFinite(midpointSeconds) || !double.IsFinite(radiusSeconds))
            throw new ArgumentException("Chebyshev time normalization requires finite inputs.");
        if (radiusSeconds <= 0.0)
            throw new ArgumentException("Chebyshev record radius must be positive.");
        return (epochSeconds - midpointSeconds) / radiusSeconds;
    }

    /// <summary>
    /// Evaluates sum c_n T_n(u) with the Clenshaw recurrence (numerically stable).
    /// coefficients[0] is c_0. Requires at least one coefficient.
    /// </summary>
    public static double Evaluate(ReadOnlySpan<double> coefficients, double u)
    {
        Validate(coefficients, u);
        // Clenshaw: b_{n+1} = b_{n+2} = 0; b_k = c_k + 2u b_{k+1} - b_{k+2}
        // for k = n..1; value = c_0 + u b_1 - b_2. c_0 stays outside the loop.
        double b2 = 0.0;
        double b1 = 0.0;
        for (int k = coefficients.Length - 1; k >= 1; k--)
        {
            double b0 = coefficients[k] + 2.0 * u * b1 - b2;
            b2 = b1;
            b1 = b0;
        }
        return coefficients[0] + u * b1 - b2;
    }

    /// <summary>
    /// Evaluates the polynomial value and its derivative d/du at u.
    /// Callers scale the derivative by 1/radius to obtain d/dt.
    /// </summary>
    public static (double Value, double DerivativeWrtU) EvaluateWithDerivative(
        ReadOnlySpan<double> coefficients, double u)
    {
        Validate(coefficients, u);
        // Differentiated Clenshaw: d_k = 2 b_{k+1} + 2u d_{k+1} - d_{k+2};
        // value = c_0 + u b_1 - b_2; derivative = b_1 + u d_1 - d_2.
        double b2 = 0.0;
        double b1 = 0.0;
        double d2 = 0.0;
        double d1 = 0.0;
        for (int k = coefficients.Length - 1; k >= 1; k--)
        {
            double b0 = coefficients[k] + 2.0 * u * b1 - b2;
            double d0 = 2.0 * b1 + 2.0 * u * d1 - d2;
            b2 = b1;
            b1 = b0;
            d2 = d1;
            d1 = d0;
        }
        double value = coefficients[0] + u * b1 - b2;
        double derivative = b1 + u * d1 - d2;
        return (value, derivative);
    }

    private static void Validate(ReadOnlySpan<double> coefficients, double u)
    {
        if (coefficients.Length == 0)
            throw new ArgumentException("Chebyshev coefficient set must not be empty.");
        if (!double.IsFinite(u))
            throw new ArgumentException("Chebyshev argument must be finite.");
        for (int i = 0; i < coefficients.Length; i++)
        {
            if (!double.IsFinite(coefficients[i]))
                throw new ArgumentException($"Chebyshev coefficient [{i}] is not finite.");
        }
    }
}
