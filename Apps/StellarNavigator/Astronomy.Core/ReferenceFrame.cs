namespace Astronomy.Core;

public enum ReferenceFrameKind
{
    IcrfJ2000,
    Heliocentric,
    Barycentric,
    PlanetCentered,
    SpacecraftBody
}

public readonly struct ReferenceFrame : IEquatable<ReferenceFrame>
{
    public ReferenceFrameKind Kind { get; }
    public string? CenterBodyId { get; }

    private ReferenceFrame(ReferenceFrameKind kind, string? centerBodyId)
    {
        Kind = kind;
        CenterBodyId = centerBodyId;
    }

    public static ReferenceFrame IcrfJ2000 => new(ReferenceFrameKind.IcrfJ2000, null);

    public static ReferenceFrame Heliocentric => new(ReferenceFrameKind.Heliocentric, "sun");

    public static ReferenceFrame Barycentric => new(ReferenceFrameKind.Barycentric, "ssb");

    public static ReferenceFrame PlanetCentered(string planetId) =>
        new(ReferenceFrameKind.PlanetCentered, planetId);

    public bool Equals(ReferenceFrame other) =>
        Kind == other.Kind && string.Equals(CenterBodyId, other.CenterBodyId, StringComparison.Ordinal);

    public override bool Equals(object? obj) => obj is ReferenceFrame other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(Kind, CenterBodyId);

    public override string ToString() =>
        Kind switch
        {
            ReferenceFrameKind.IcrfJ2000 => "ICRF/J2000",
            ReferenceFrameKind.Heliocentric => "Heliocentric",
            ReferenceFrameKind.Barycentric => "Solar System Barycentric",
            ReferenceFrameKind.PlanetCentered => $"PlanetCentered({CenterBodyId})",
            ReferenceFrameKind.SpacecraftBody => $"SpacecraftBody({CenterBodyId})",
            _ => Kind.ToString()
        };
}
