using Astronomy.Core;

namespace Astronomy.Catalog;

public interface IAstronomyCatalog
{
    IReadOnlyList<Body> GetAllBodies();
    Body? GetBody(string id);
    IReadOnlyList<Body> GetChildren(string parentId);
    IReadOnlyList<Body> GetByKind(BodyKind kind);
}
