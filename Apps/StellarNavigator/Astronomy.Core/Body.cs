namespace Astronomy.Core;

public enum BodyKind
{
    Star,
    Planet,
    NaturalSatellite,
    Barycenter,
    Spacecraft,
    Observer
}

public sealed class Body
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required BodyKind Kind { get; init; }
    public string? ParentId { get; init; }
    public PhysicalProperties Properties { get; init; } = PhysicalProperties.Empty();
    public List<string> ReferenceIds { get; init; } = new();
    public string? Description { get; init; }

    public override string ToString() => $"{Name} ({Id}) [{Kind}]";
}
