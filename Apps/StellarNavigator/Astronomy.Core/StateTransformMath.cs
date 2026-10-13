namespace Astronomy.Core;

/// <summary>
/// SN6 six-component Cartesian state-transformation mathematics.
///
/// For a time-dependent rotation R(t):
///   r_dest = R(t) * r_src
///   v_dest = R(t) * v_src + dR(t)/dt * r_src
/// i.e. the 6x6 block structure [[R, 0], [R', R]].
///
/// Three operations are kept distinct by name so callers cannot confuse them:
/// <list type="bullet">
/// <item><see cref="RotatePosition"/>: 3x3 position-only rotation (NO velocity).</item>
/// <item><see cref="TransformState"/>: full 6-component state transformation.</item>
/// <item><see cref="InverseTransformState"/>: proper inverse WITH velocity
/// coupling (never a transpose of the 6x6 matrix).</item>
/// </list>
/// Origin translation is a separate operation
/// (Navigation.Core OriginTranslation); it is not folded into these
/// orientation transforms. All quantities are double precision, SI units
/// (m, m/s), finite-validated.
/// </summary>
public static class StateTransformMath
{
    /// <summary>
    /// Position-only rotation r' = R*r. The result carries no velocity
    /// information; do not use it where a state transform is required.
    /// </summary>
    public static Vector3D RotatePosition(RotationMatrix r, Vector3D position)
    {
        r.RequireFinite();
        return r.RotatePosition(position);
    }

    /// <summary>
    /// Full six-component state transformation.
    /// </summary>
    public static (Vector3D Position, Vector3D Velocity) TransformState(
        RotationMatrix r, RotationMatrix.Derivative rDot,
        Vector3D position, Vector3D velocity)
    {
        r.RequireFinite();
        RotationMatrix.RequireFiniteVector(position, nameof(position));
        RotationMatrix.RequireFiniteVector(velocity, nameof(velocity));
        var rp = r.RotatePosition(position);
        var rv = r.RotatePosition(velocity);
        var coupling = rDot.Multiply(position);
        return (rp, new Vector3D(rv.X + coupling.X, rv.Y + coupling.Y, rv.Z + coupling.Z));
    }

    /// <summary>
    /// Proper inverse state transformation:
    ///   r_src = R^T * r_dest
    ///   v_src = R^T * v_dest - R^T * R' * R^T * r_dest
    /// The 6x6 matrix is NOT transposed as a whole (that would move R'
    /// to the wrong block and negate nothing correctly).
    /// </summary>
    public static (Vector3D Position, Vector3D Velocity) InverseTransformState(
        RotationMatrix r, RotationMatrix.Derivative rDot,
        Vector3D positionDest, Vector3D velocityDest)
    {
        r.RequireFinite();
        RotationMatrix.RequireFiniteVector(positionDest, nameof(positionDest));
        RotationMatrix.RequireFiniteVector(velocityDest, nameof(velocityDest));
        var rt = r.Transpose();
        var posSrc = rt.RotatePosition(positionDest);
        // R^T * R' * r_src, where r_src = R^T * r_dest.
        var rPrimeRSrc = rDot.Multiply(posSrc);
        var correction = rt.RotatePosition(rPrimeRSrc);
        var vRot = rt.RotatePosition(velocityDest);
        return (posSrc, new Vector3D(vRot.X - correction.X, vRot.Y - correction.Y, vRot.Z - correction.Z));
    }

    /// <summary>
    /// 6x6 state-transform matrix in row-major order (rows 0-2 position,
    /// rows 3-5 velocity): [[R, 0], [Rdot, R]]. For diagnostics/fixtures.
    /// </summary>
    public static double[,] ToStateMatrix(RotationMatrix r, RotationMatrix.Derivative rDot)
    {
        r.RequireFinite();
        var m = new double[6, 6];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
            {
                m[i, j] = r[i, j];
                m[i + 3, j + 3] = r[i, j];
            }
        m[3, 0] = rDot.D00; m[3, 1] = rDot.D01; m[3, 2] = rDot.D02;
        m[4, 0] = rDot.D10; m[4, 1] = rDot.D11; m[4, 2] = rDot.D12;
        m[5, 0] = rDot.D20; m[5, 1] = rDot.D21; m[5, 2] = rDot.D22;
        return m;
    }

    /// <summary>
    /// Composes two successive orientation transforms: first <paramref name="first"/>,
    /// then <paramref name="second"/> (each with its derivative at the same epoch).
    /// R = R2*R1; R' = R2'*R1 + R2*R1'.
    /// </summary>
    public static (RotationMatrix R, RotationMatrix.Derivative RDot) Compose(
        RotationMatrix r1, RotationMatrix.Derivative d1,
        RotationMatrix r2, RotationMatrix.Derivative d2)
    {
        r1.RequireFinite(); r2.RequireFinite();
        var r = r2.Multiply(r1);
        // R' entries via (R2'*R1 + R2*R1') expanded on components.
        double[,] a = { { r1[0, 0], r1[0, 1], r1[0, 2] }, { r1[1, 0], r1[1, 1], r1[1, 2] }, { r1[2, 0], r1[2, 1], r1[2, 2] } };
        double[,] b = { { r2[0, 0], r2[0, 1], r2[0, 2] }, { r2[1, 0], r2[1, 1], r2[1, 2] }, { r2[2, 0], r2[2, 1], r2[2, 2] } };
        double[,] da = { { d1.D00, d1.D01, d1.D02 }, { d1.D10, d1.D11, d1.D12 }, { d1.D20, d1.D21, d1.D22 } };
        double[,] db = { { d2.D00, d2.D01, d2.D02 }, { d2.D10, d2.D11, d2.D12 }, { d2.D20, d2.D21, d2.D22 } };
        double[,] rd = new double[3, 3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
            {
                double s = 0.0;
                for (int k = 0; k < 3; k++)
                    s += db[i, k] * a[k, j] + b[i, k] * da[k, j];
                rd[i, j] = s;
            }
        var rdot = new RotationMatrix.Derivative(
            rd[0, 0], rd[0, 1], rd[0, 2],
            rd[1, 0], rd[1, 1], rd[1, 2],
            rd[2, 0], rd[2, 1], rd[2, 2]);
        return (r, rdot);
    }
}
