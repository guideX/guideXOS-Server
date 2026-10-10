namespace Astronomy.Core;

public readonly struct Vector3D : IEquatable<Vector3D>
{
    public double X { get; }
    public double Y { get; }
    public double Z { get; }

    public Vector3D(double x, double y, double z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public double Magnitude => Math.Sqrt(X * X + Y * Y + Z * Z);

    public Vector3D Normalized()
    {
        var mag = Magnitude;
        if (mag == 0) throw new InvalidOperationException("Cannot normalize zero vector");
        return new Vector3D(X / mag, Y / mag, Z / mag);
    }

    public static Vector3D operator +(Vector3D a, Vector3D b) =>
        new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);

    public static Vector3D operator -(Vector3D a, Vector3D b) =>
        new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);

    public static Vector3D operator -(Vector3D a) => new(-a.X, -a.Y, -a.Z);

    public static Vector3D operator *(Vector3D v, double scalar) =>
        new(v.X * scalar, v.Y * scalar, v.Z * scalar);

    public static Vector3D operator /(Vector3D v, double scalar) =>
        new(v.X / scalar, v.Y / scalar, v.Z / scalar);

    public double DistanceTo(Vector3D other) => (this - other).Magnitude;

    public override string ToString() => $"({X:R}, {Y:R}, {Z:R})";

    public bool Equals(Vector3D other) =>
        X.Equals(other.X) && Y.Equals(other.Y) && Z.Equals(other.Z);

    public override bool Equals(object? obj) => obj is Vector3D other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
}
