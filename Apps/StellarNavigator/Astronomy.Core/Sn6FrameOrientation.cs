namespace Astronomy.Core;

/// <summary>
/// SN6 axis-orientation identifiers. Only these two orientations are
/// supported. Orientation is deliberately separate from coordinate origin:
/// a state is located by (orientation, origin, epoch) jointly.
/// </summary>
public enum FrameOrientation
{
    /// <summary>Inertial, Earth-mean-equator J2000 axes (NAIF frame 1).</summary>
    J2000,
    /// <summary>
    /// Earth body-fixed axes from the authenticated IAU 2015 polynomial
    /// model in NAIF generic text PCK pck00011.tpc (NAIF frame 130399).
    /// Low-precision cartographic model; NOT ITRF93 or any EOP product.
    /// </summary>
    IauEarth,
}

/// <summary>
/// Supported position/velocity units for SN6 Cartesian states.
/// The runtime evaluates in SI (metre, metre/second); any other unit
/// label on an input state is rejected, never silently converted.
/// </summary>
public enum Sn6PositionUnit { Metre }

/// <summary>Supported velocity units for SN6 Cartesian states (SI only).</summary>
public enum Sn6VelocityUnit { MetrePerSecond }

/// <summary>
/// Limited, explicit SN6 frame registry. Arbitrary frame names are never
/// accepted and unsupported names never fall back to J2000.
/// </summary>
public static class FrameRegistry
{
    /// <summary>Canonical supported orientation names.</summary>
    public static IReadOnlyList<string> SupportedOrientations { get; } =
        new[] { "J2000", "IAU_EARTH" };

    public static bool TryParseOrientation(string? name, out FrameOrientation orientation)
    {
        if (string.Equals(name, "J2000", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(name, "ICRF_J2000", StringComparison.OrdinalIgnoreCase))
        {
            orientation = FrameOrientation.J2000;
            return true;
        }
        if (string.Equals(name, "IAU_EARTH", StringComparison.Ordinal))
        {
            orientation = FrameOrientation.IauEarth;
            return true;
        }
        orientation = default;
        return false;
    }

    /// <summary>
    /// Diagnostic for an unsupported frame request. Never returns a
    /// fallback orientation.
    /// </summary>
    public static string UnsupportedDiagnostic(string? name)
    {
        if (string.Equals(name, "ITRF93", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(name, "ITRF", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(name, "EARTH_FIXED", StringComparison.OrdinalIgnoreCase))
            return $"Frame '{name}' is not supported: SN6 provides IAU_EARTH " +
                "(low-precision IAU cartographic model), which is NOT equivalent " +
                "to high-precision terrestrial frames (ITRF93/EOP-based products) " +
                "and must not be substituted for them. Supported: J2000, IAU_EARTH.";
        return $"Frame '{name}' is not supported and no fallback is applied. " +
            "Supported: J2000, IAU_EARTH.";
    }
}
