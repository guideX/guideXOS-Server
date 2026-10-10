using Astronomy.Catalog;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;

var catalogPath = Path.Combine(AppContext.BaseDirectory, "Data", "solar_system.json");
var ephemerisPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_j2000.json");

Console.WriteLine("=== Stellar Navigator SN1 ===");
Console.WriteLine("Spacecraft Navigation Architecture - Initial Foundation");
Console.WriteLine();

var catalog = JsonCatalog.Load(catalogPath);
var ephemeris = new PrecomputedEphemerisProvider(ephemerisPath);

Console.WriteLine($"Catalog loaded: {catalog.GetAllBodies().Count} bodies");
Console.WriteLine($"Ephemeris source: {ephemeris.DataSource}");
Console.WriteLine($"Ephemeris frame: {ephemeris.NativeFrame}");
Console.WriteLine();

var epoch = AstroTime.FromJ2000Days(0);
Console.WriteLine($"Simulation epoch: {epoch}");
Console.WriteLine();

Console.WriteLine("--- Celestial Bodies ---");
foreach (var body in catalog.GetAllBodies())
{
    var props = body.Properties;
    var info = $"{body.Name,-20} [{body.Kind,-18}]";
    if (props.RadiusMeters.HasValue)
        info += $" R={props.RadiusMeters.Value / 1000.0:F0}km";
    if (props.MassKg.HasValue)
        info += $" M={props.MassKg.Value:E2}kg";
    Console.WriteLine(info);
}

Console.WriteLine();
Console.WriteLine("--- Simulated Spacecraft State ---");
var scState = new SpacecraftState
{
    SpacecraftId = "sim spacecraft",
    Position = new Vector3D(0, 0, 0),
    Velocity = new Vector3D(0, 0, 0),
    Frame = ReferenceFrame.Heliocentric,
    CenterOfReferenceId = "sun",
    Epoch = epoch,
    Provenance = StateProvenance.Simulated,
    Quality = NavigationResultStatus.ApproximateOrSimulated
};
Console.WriteLine(scState);
Console.WriteLine("NOTE: Spacecraft position is synthetic demonstration data.");
Console.WriteLine();

Console.WriteLine("--- Heliocentric Positions (J2000) ---");
var heliocentric = ReferenceFrame.Heliocentric;
foreach (var body in catalog.GetByKind(BodyKind.Planet))
{
    var result = ephemeris.GetState(body.Id, epoch, heliocentric);
    if (result.IsSuccess && result.Value is var (pos, vel))
    {
        var distAu = pos.Magnitude / Distance.MetersPerAstronomicalUnit;
        Console.WriteLine($"{body.Name,-10} r={distAu:F3} AU  v={vel.Magnitude / 1000.0:F2} km/s  [{result.Status}]");
    }
    else
    {
        Console.WriteLine($"{body.Name,-10} UNAVAILABLE: {result.ErrorMessage}");
    }
}

Console.WriteLine();
Console.WriteLine("--- Earth-Moon Distance ---");
var earthResult = ephemeris.GetState("earth", epoch, heliocentric);
var moonResult = ephemeris.GetState("moon", epoch, heliocentric);
if (earthResult.IsSuccess && moonResult.IsSuccess)
{
    var (earthPos, _) = earthResult.Value!;
    var (moonPos, _) = moonResult.Value!;
    var distKm = earthPos.DistanceTo(moonPos) / 1000.0;
    Console.WriteLine($"Earth-Moon distance: {distKm:F0} km [approximate demonstration data]");
}

Console.WriteLine();
Console.WriteLine("--- Data Quality Summary ---");
Console.WriteLine("All ephemeris data is synthetic demonstration data.");
Console.WriteLine("No real spacecraft positions are used.");
Console.WriteLine("No navigation solution is validated for flight use.");
Console.WriteLine();
Console.WriteLine("Stellar Navigator SN1 - Offline operation confirmed.");
