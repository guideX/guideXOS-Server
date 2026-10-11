using Astronomy.Catalog;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;

var catalogPath = Path.Combine(AppContext.BaseDirectory, "Data", "solar_system.json");
var ephemerisPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s.json");

Console.WriteLine("=== Stellar Navigator SN2 ===");
Console.WriteLine("Real Ephemerides & Solar System Positioning");
Console.WriteLine();

var catalog = JsonCatalog.Load(catalogPath);
var ephemeris = new JplEphemerisProvider(ephemerisPath);

Console.WriteLine($"Catalog loaded: {catalog.GetAllBodies().Count} bodies");
Console.WriteLine($"Ephemeris source: {ephemeris.DataSource}");
Console.WriteLine($"Ephemeris frame: {ephemeris.NativeFrame}");
Console.WriteLine($"Ephemeris SHA-256: {ephemeris.Sha256[..16]}...");
Console.WriteLine($"Coverage: {ephemeris.CoverageStart.JdUtc:F6} to {ephemeris.CoverageEnd.JdUtc:F6} JD");
Console.WriteLine($"Supported bodies: {string.Join(", ", ephemeris.SupportedBodies)}");
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
    Frame = ReferenceFrame.Barycentric,
    CenterOfReferenceId = "ssb",
    Epoch = epoch,
    Provenance = StateProvenance.Simulated,
    Quality = NavigationResultStatus.ApproximateOrSimulated
};
Console.WriteLine(scState);
Console.WriteLine("NOTE: Spacecraft position is synthetic demonstration data.");
Console.WriteLine();

Console.WriteLine("--- JPL Ephemeris Positions (J2000) ---");
var barycentric = ReferenceFrame.Barycentric;
foreach (var body in catalog.GetByKind(BodyKind.Planet))
{
    var result = ephemeris.GetState(body.Id, epoch, barycentric);
    if (result.IsSuccess && result.Value is var (pos, vel))
    {
        var distAu = pos.Magnitude / Distance.MetersPerAstronomicalUnit;
        Console.WriteLine($"{body.Name,-10} r={distAu:F6} AU  v={vel.Magnitude / 1000.0:F4} km/s  [{result.Status}]");
    }
    else
    {
        Console.WriteLine($"{body.Name,-10} UNAVAILABLE: {result.ErrorMessage}");
    }
}

Console.WriteLine();
Console.WriteLine("--- Relative Position Calculations ---");
var earthResult = ephemeris.GetState("earth", epoch, barycentric);
var moonResult = ephemeris.GetState("moon", epoch, barycentric);
var marsResult = ephemeris.GetState("mars", epoch, barycentric);
var jupiterResult = ephemeris.GetState("jupiter", epoch, barycentric);

if (earthResult.IsSuccess && moonResult.IsSuccess)
{
    var (earthPos, _) = earthResult.Value!;
    var (moonPos, _) = moonResult.Value!;
    var distKm = earthPos.DistanceTo(moonPos) / 1000.0;
    Console.WriteLine($"Earth-Moon distance: {distKm:F1} km [JPL DE440s]");
}

if (earthResult.IsSuccess && marsResult.IsSuccess)
{
    var (earthPos, _) = earthResult.Value!;
    var (marsPos, _) = marsResult.Value!;
    var distKm = earthPos.DistanceTo(marsPos) / 1000.0;
    Console.WriteLine($"Earth-Mars distance: {distKm:F1} km [JPL DE440s]");
}

if (earthResult.IsSuccess && jupiterResult.IsSuccess)
{
    var (earthPos, _) = earthResult.Value!;
    var (jupiterPos, _) = jupiterResult.Value!;
    var distKm = earthPos.DistanceTo(jupiterPos) / 1000.0;
    Console.WriteLine($"Earth-Jupiter distance: {distKm:F1} km [JPL DE440s]");
}

Console.WriteLine();
Console.WriteLine("--- Data Quality Summary ---");
Console.WriteLine("Ephemeris data: NASA/JPL DE440s planetary ephemeris");
Console.WriteLine("Frame: J2000 (ICRF), Origin: Solar System Barycenter");
Console.WriteLine("Time scale: TDB, Aberration correction: NONE (geometric states)");
Console.WriteLine("Spacecraft data: Simulated (not measured)");
Console.WriteLine("No navigation solution is validated for flight use.");
Console.WriteLine();
Console.WriteLine("Stellar Navigator SN2 - Offline operation confirmed.");

// ─── SN3: continuous Chebyshev evaluation at an arbitrary TDB epoch ───
Console.WriteLine();
Console.WriteLine("=== Stellar Navigator SN3 ===");
Console.WriteLine("Continuous Chebyshev evaluation (pure managed code, offline)");

var chebyPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s_chebyshev.json");
if (!File.Exists(chebyPath))
{
    Console.WriteLine($"SN3 dataset missing: {chebyPath}");
    Console.WriteLine("SN3 continuous evaluation is unavailable; SN2 exact-sample data above remains valid.");
    return;
}

var continuous = new ChebyshevEphemerisProvider(chebyPath);
Console.WriteLine($"Continuous source: {continuous.DataSource}");
Console.WriteLine($"Records: {continuous.TotalRecords}, SHA-256: {continuous.Sha256[..16]}...");
Console.WriteLine($"Coverage: {continuous.CoverageStart.JdUtc:F6} to {continuous.CoverageEnd.JdUtc:F6} JD (TDB)");

// Minimal off-grid epoch selection: CLI args [bodyId] [jdTdb], else defaults
// deliberately off the SN2 30-day sample grid.
string targetId = args.Length > 0 ? args[0] : "mars";
double jdTdb = args.Length > 1 && double.TryParse(args[1],
    System.Globalization.NumberStyles.Float,
    System.Globalization.CultureInfo.InvariantCulture, out var parsed)
    ? parsed
    : 2451545.0 + 12.3456789;
var offGridEpoch = new AstroTime(jdTdb, TimeScale.Tdb);
Console.WriteLine($"Requested body: {targetId}, TDB epoch: JD {jdTdb:F7}");

var state = continuous.GetState(targetId, offGridEpoch, barycentric);
if (state.IsSuccess && state.Value is var (cpos, cvel))
{
    Console.WriteLine($"{targetId} position (km): ({cpos.X / 1000.0:F3}, {cpos.Y / 1000.0:F3}, {cpos.Z / 1000.0:F3})");
    Console.WriteLine($"{targetId} velocity (km/s): ({cvel.X / 1000.0:F6}, {cvel.Y / 1000.0:F6}, {cvel.Z / 1000.0:F6})");
    Console.WriteLine($"Source: {state.Provenance}  Quality: {state.Status}");

    string otherId = targetId == "earth" ? "mars" : "earth";
    var relCalc = new RelativePositionCalculator(continuous);
    var rel = relCalc.Calculate(targetId, otherId, offGridEpoch);
    if (rel.IsSuccess)
        Console.WriteLine($"{targetId}-{otherId} distance: {rel.Value!.SeparationDistanceKm:F3} km [{rel.Value.Status}]");
    else
        Console.WriteLine($"Distance unavailable: {rel.ErrorMessage}");
}
else
{
    Console.WriteLine($"UNAVAILABLE [{state.Status}]: {state.ErrorMessage}");
}

// Explicit failure demonstration: epoch beyond supported coverage.
var farFuture = new AstroTime(2506352.5 + 365.0, TimeScale.Tdb);
var oob = continuous.GetState("earth", farFuture, barycentric);
Console.WriteLine($"Out-of-coverage query: Success={oob.IsSuccess} [{oob.Status}] {oob.ErrorMessage}");
Console.WriteLine();
Console.WriteLine("Stellar Navigator SN3 - Continuous offline evaluation confirmed.");
