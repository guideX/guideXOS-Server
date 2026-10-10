using Astronomy.Catalog;
using Astronomy.Core;
using Xunit;

namespace StellarNavigator.Tests;

public class CatalogTests
{
    private static JsonCatalog CreateCatalog() =>
        JsonCatalog.LoadFromString("""
        {
          "schemaVersion": 1,
          "bodies": [
            { "id": "sun", "name": "Sun", "kind": "Star", "parentId": null,
              "properties": { "radiusMeters": 695700000.0, "massKg": 1.98892e30, "muM3S2": 1.32712440018e20, "dataSource": "NASA/JPL" },
              "referenceIds": ["NAIF:10"] },
            { "id": "earth", "name": "Earth", "kind": "Planet", "parentId": "sun",
              "properties": { "radiusMeters": 6371000.0, "massKg": 5.9722e24, "muM3S2": 3.986004418e14, "dataSource": "NASA/JPL" },
              "referenceIds": ["NAIF:399"] },
            { "id": "moon", "name": "Moon", "kind": "NaturalSatellite", "parentId": "earth",
              "properties": { "radiusMeters": 1737400.0, "massKg": 7.342e22, "muM3S2": 4.9048695e12, "dataSource": "NASA/JPL" },
              "referenceIds": ["NAIF:301"] },
            { "id": "sim", "name": "Sim Spacecraft", "kind": "Spacecraft", "parentId": null,
              "properties": { "massKg": 1000.0, "dataSource": "Synthetic" }, "referenceIds": [] }
          ]
        }
        """);

    [Fact]
    public void Catalog_LoadsAllBodies()
    {
        var catalog = CreateCatalog();
        Assert.Equal(4, catalog.GetAllBodies().Count);
    }

    [Fact]
    public void Catalog_StableIdentification()
    {
        var catalog = CreateCatalog();
        var earth = catalog.GetBody("earth");
        Assert.NotNull(earth);
        Assert.Equal("Earth", earth.Name);
        Assert.Equal(BodyKind.Planet, earth.Kind);
        Assert.Equal("sun", earth.ParentId);
    }

    [Fact]
    public void Catalog_ParentChildRelationships()
    {
        var catalog = CreateCatalog();
        var children = catalog.GetChildren("earth");
        Assert.Single(children);
        Assert.Equal("moon", children[0].Id);
    }

    [Fact]
    public void Catalog_FilterByKind()
    {
        var catalog = CreateCatalog();
        var planets = catalog.GetByKind(BodyKind.Planet);
        Assert.Single(planets);
        Assert.Equal("earth", planets[0].Id);
    }

    [Fact]
    public void Catalog_MissingBodyReturnsNull()
    {
        var catalog = CreateCatalog();
        Assert.Null(catalog.GetBody("nonexistent"));
    }

    [Fact]
    public void Catalog_PhysicalPropertiesPreserved()
    {
        var catalog = CreateCatalog();
        var sun = catalog.GetBody("sun")!;
        Assert.Equal(695700000.0, sun.Properties.RadiusMeters);
        Assert.Equal(1.98892e30, sun.Properties.MassKg);
        Assert.Equal(1.32712440018e20, sun.Properties.GravitationalParameterM3S2);
        Assert.Equal("NASA/JPL", sun.Properties.DataSource);
    }
}
