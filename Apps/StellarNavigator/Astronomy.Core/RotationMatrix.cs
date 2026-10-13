namespace Astronomy.Core;

/// <summary>
/// SN6 3x3 orientation matrix (double precision, row-major).
/// Represents a single axis-orientation change (frame rotation):
/// r_destination = R * r_source. It carries NO velocity coupling and
/// must never be mistaken for a full six-component state transform.
/// </summary>
public readonly struct RotationMatrix
{
    private readonly double _m00, _m01, _m02;
    private readonly double _m10, _m11, _m12;
    private readonly double _m20, _m21, _m22;

    public RotationMatrix(
        double m00, double m01, double m02,
        double m10, double m11, double m12,
        double m20, double m21, double m22)
    {
        _m00 = m00; _m01 = m01; _m02 = m02;
        _m10 = m10; _m11 = m11; _m12 = m12;
        _m20 = m20; _m21 = m21; _m22 = m22;
    }

    public double this[int row, int col]
    {
        get
        {
            if ((uint)row > 2 || (uint)col > 2)
                throw new ArgumentOutOfRangeException("Matrix indices must be 0..2.");
            return (row, col) switch
            {
                (0, 0) => _m00, (0, 1) => _m01, (0, 2) => _m02,
                (1, 0) => _m10, (1, 1) => _m11, (1, 2) => _m12,
                _ => row == 2 && col == 0 ? _m20 : row == 2 && col == 1 ? _m21 : _m22,
            };
        }
    }

    public static RotationMatrix Identity =>
        new(1, 0, 0, 0, 1, 0, 0, 0, 1);

    /// <summary>Position-only rotation: r' = R * r. No velocity implied.</summary>
    public Vector3D RotatePosition(Vector3D v)
    {
        RequireFinite();
        RequireFiniteVector(v, nameof(v));
        return new Vector3D(
            _m00 * v.X + _m01 * v.Y + _m02 * v.Z,
            _m10 * v.X + _m11 * v.Y + _m12 * v.Z,
            _m20 * v.X + _m21 * v.Y + _m22 * v.Z);
    }

    /// <summary>Proper inverse of a rotation: transpose.</summary>
    public RotationMatrix Transpose() =>
        new(_m00, _m10, _m20, _m01, _m11, _m21, _m02, _m12, _m22);

    public RotationMatrix Multiply(RotationMatrix other)
    {
        RequireFinite();
        other.RequireFinite();
        return new RotationMatrix(
            _m00 * other._m00 + _m01 * other._m10 + _m02 * other._m20,
            _m00 * other._m01 + _m01 * other._m11 + _m02 * other._m21,
            _m00 * other._m02 + _m01 * other._m12 + _m02 * other._m22,
            _m10 * other._m00 + _m11 * other._m10 + _m12 * other._m20,
            _m10 * other._m01 + _m11 * other._m11 + _m12 * other._m21,
            _m10 * other._m02 + _m11 * other._m12 + _m12 * other._m22,
            _m20 * other._m00 + _m21 * other._m10 + _m22 * other._m20,
            _m20 * other._m01 + _m21 * other._m11 + _m22 * other._m21,
            _m20 * other._m02 + _m21 * other._m12 + _m22 * other._m22);
    }

    /// <summary>Maximum |R*R^T - I| element (orthogonality check).</summary>
    public double OrthogonalityError()
    {
        var rt = Transpose();
        var p = Multiply(rt);
        double max = 0.0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
            {
                double expect = i == j ? 1.0 : 0.0;
                max = Math.Max(max, Math.Abs(p[i, j] - expect));
            }
        return max;
    }

    public double Determinant() =>
        _m00 * (_m11 * _m22 - _m12 * _m21)
        - _m01 * (_m10 * _m22 - _m12 * _m20)
        + _m02 * (_m10 * _m21 - _m11 * _m20);

    public void RequireFinite()
    {
        if (!double.IsFinite(_m00) || !double.IsFinite(_m01) || !double.IsFinite(_m02) ||
            !double.IsFinite(_m10) || !double.IsFinite(_m11) || !double.IsFinite(_m12) ||
            !double.IsFinite(_m20) || !double.IsFinite(_m21) || !double.IsFinite(_m22))
            throw new InvalidOperationException("Rotation matrix contains non-finite values.");
    }

    internal static void RequireFiniteVector(Vector3D v, string name)
    {
        if (!double.IsFinite(v.X) || !double.IsFinite(v.Y) || !double.IsFinite(v.Z))
            throw new ArgumentException($"Vector '{name}' contains non-finite components.", name);
    }

    /// <summary>
    /// Time derivative dR/dt of a rotation evaluated analytically by the
    /// caller (e.g. IAU_EARTH). Stored alongside R for state transforms;
    /// NOT a rotation itself.
    /// </summary>
    public readonly struct Derivative
    {
        public double D00 { get; } public double D01 { get; } public double D02 { get; }
        public double D10 { get; } public double D11 { get; } public double D12 { get; }
        public double D20 { get; } public double D21 { get; } public double D22 { get; }

        public Derivative(
            double d00, double d01, double d02,
            double d10, double d11, double d12,
            double d20, double d21, double d22)
        {
            D00 = d00; D01 = d01; D02 = d02;
            D10 = d10; D11 = d11; D12 = d12;
            D20 = d20; D21 = d21; D22 = d22;
        }

        public Vector3D Multiply(Vector3D v)
        {
            RotationMatrix.RequireFiniteVector(v, nameof(v));
            if (!double.IsFinite(D00) || !double.IsFinite(D01) || !double.IsFinite(D02) ||
                !double.IsFinite(D10) || !double.IsFinite(D11) || !double.IsFinite(D12) ||
                !double.IsFinite(D20) || !double.IsFinite(D21) || !double.IsFinite(D22))
                throw new InvalidOperationException("Rotation derivative contains non-finite values.");
            return new Vector3D(
                D00 * v.X + D01 * v.Y + D02 * v.Z,
                D10 * v.X + D11 * v.Y + D12 * v.Z,
                D20 * v.X + D21 * v.Y + D22 * v.Z);
        }
    }
}
