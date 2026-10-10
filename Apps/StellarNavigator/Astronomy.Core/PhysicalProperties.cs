namespace Astronomy.Core;

public sealed class PhysicalProperties
{
    public double? RadiusMeters { get; init; }
    public double? MassKg { get; init; }
    public double? GravitationalParameterM3S2 { get; init; }
    public string? DataSource { get; init; }
    public DateTime? DataRetrievalDate { get; init; }

    public static PhysicalProperties Empty() => new();

    public static PhysicalProperties WithRadius(double radiusMeters) =>
        new() { RadiusMeters = radiusMeters };

    public static PhysicalProperties WithMass(double massKg) =>
        new() { MassKg = massKg };

    public static PhysicalProperties WithMu(double muM3S2) =>
        new() { GravitationalParameterM3S2 = muM3S2 };

    public PhysicalProperties WithSource(string source, DateTime? retrievalDate = null) =>
        new()
        {
            RadiusMeters = RadiusMeters,
            MassKg = MassKg,
            GravitationalParameterM3S2 = GravitationalParameterM3S2,
            DataSource = source,
            DataRetrievalDate = retrievalDate
        };

    public override string ToString()
    {
        var parts = new List<string>();
        if (RadiusMeters.HasValue) parts.Add($"R={RadiusMeters.Value / 1000.0:F1} km");
        if (MassKg.HasValue) parts.Add($"M={MassKg.Value:E3} kg");
        if (GravitationalParameterM3S2.HasValue) parts.Add($"mu={GravitationalParameterM3S2.Value:E6} m^3/s^2");
        return string.Join(", ", parts);
    }
}
