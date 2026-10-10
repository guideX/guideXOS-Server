using System.Text.Json;
using Astronomy.Core;

namespace Astronomy.Catalog;

public sealed class JsonCatalog : IAstronomyCatalog
{
    private readonly Dictionary<string, Body> _bodies = new(StringComparer.Ordinal);

    public static JsonCatalog Load(string jsonPath)
    {
        var json = File.ReadAllText(jsonPath);
        return LoadFromString(json);
    }

    public static JsonCatalog LoadFromString(string json)
    {
        var catalog = new JsonCatalog();
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (!root.TryGetProperty("bodies", out var bodiesEl) || bodiesEl.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Catalog JSON must contain a 'bodies' array");

        foreach (var bodyEl in bodiesEl.EnumerateArray())
        {
            var body = ParseBody(bodyEl);
            catalog._bodies[body.Id] = body;
        }

        return catalog;
    }

    private static Body ParseBody(JsonElement el)
    {
        var id = el.GetProperty("id").GetString() ?? throw new InvalidDataException("Body missing 'id'");
        var name = el.GetProperty("name").GetString() ?? throw new InvalidDataException($"Body {id} missing 'name'");
        var kindStr = el.GetProperty("kind").GetString() ?? throw new InvalidDataException($"Body {id} missing 'kind'");
        var kind = Enum.Parse<BodyKind>(kindStr, ignoreCase: true);

        string? parentId = null;
        if (el.TryGetProperty("parentId", out var parentEl) && parentEl.ValueKind == JsonValueKind.String)
            parentId = parentEl.GetString();

        var props = PhysicalProperties.Empty();
        if (el.TryGetProperty("properties", out var propsEl))
        {
            double? radius = null, mass = null, mu = null;
            string? source = null;

            if (propsEl.TryGetProperty("radiusMeters", out var rEl) && rEl.ValueKind == JsonValueKind.Number)
                radius = rEl.GetDouble();
            if (propsEl.TryGetProperty("massKg", out var mEl) && mEl.ValueKind == JsonValueKind.Number)
                mass = mEl.GetDouble();
            if (propsEl.TryGetProperty("muM3S2", out var muEl) && muEl.ValueKind == JsonValueKind.Number)
                mu = muEl.GetDouble();
            if (propsEl.TryGetProperty("dataSource", out var sEl) && sEl.ValueKind == JsonValueKind.String)
                source = sEl.GetString();

            props = new PhysicalProperties
            {
                RadiusMeters = radius,
                MassKg = mass,
                GravitationalParameterM3S2 = mu,
                DataSource = source
            };
        }

        var refIds = new List<string>();
        if (el.TryGetProperty("referenceIds", out var refEl) && refEl.ValueKind == JsonValueKind.Array)
        {
            foreach (var r in refEl.EnumerateArray())
                if (r.ValueKind == JsonValueKind.String)
                    refIds.Add(r.GetString()!);
        }

        string? desc = null;
        if (el.TryGetProperty("description", out var dEl) && dEl.ValueKind == JsonValueKind.String)
            desc = dEl.GetString();

        return new Body
        {
            Id = id,
            Name = name,
            Kind = kind,
            ParentId = parentId,
            Properties = props,
            ReferenceIds = refIds,
            Description = desc
        };
    }

    public IReadOnlyList<Body> GetAllBodies() => _bodies.Values.ToList();

    public Body? GetBody(string id) => _bodies.TryGetValue(id, out var body) ? body : null;

    public IReadOnlyList<Body> GetChildren(string parentId) =>
        _bodies.Values.Where(b => string.Equals(b.ParentId, parentId, StringComparison.Ordinal)).ToList();

    public IReadOnlyList<Body> GetByKind(BodyKind kind) =>
        _bodies.Values.Where(b => b.Kind == kind).ToList();
}
