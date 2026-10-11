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

// ─── SN4: complete eight-planet coverage with bounded-memory lazy loading ───
Console.WriteLine();
Console.WriteLine("=== Stellar Navigator SN4 ===");
Console.WriteLine("Eight-planet indexed evaluation (pure managed code, offline, bounded cache)");

var indexedPath = Path.Combine(AppContext.BaseDirectory, "Data", "ephemeris_de440s_chebyshev.sn4bin");
if (!File.Exists(indexedPath))
{
    Console.WriteLine($"SN4 dataset missing: {indexedPath}");
    Console.WriteLine("SN4 indexed evaluation is unavailable; SN2/SN3 output above remains valid.");
    return;
}

using var indexed = new IndexedChebyshevEphemerisProvider(indexedPath);
Console.WriteLine($"Indexed source: {indexed.DataSource}");
Console.WriteLine($"Segments: {indexed.Segments.Count}, records: {indexed.TotalRecords}, SHA-256: {indexed.Sha256[..16]}...");
Console.WriteLine($"SPK provenance: MD5 {indexed.Provenance.SpkMd5} / SHA-256 {indexed.Provenance.SpkSha256[..16]}...");
Console.WriteLine($"Coverage: {indexed.CoverageStart.JdUtc:F6} to {indexed.CoverageEnd.JdUtc:F6} JD (TDB)");
Console.WriteLine($"Cache budget: {indexed.CacheBudgetBytes / 1048576.0:F1} MiB (retained now: {indexed.CachedBytes} bytes)");
Console.WriteLine();

// Supported planetary objects with exact NAIF identifiers. Barycenter ids
// (1,2,4,5,6,7,8) are system barycenters, NOT physical planet centers
// (199,299,499,599,699,799,899). Mercury/Venus centers are measured
// coincident with their barycenters under DE440s conventions.
var sn4Planets = new (string BodyId, int NaifId, string Label)[]
{
    ("mercury", 1, "Mercury system barycenter"),
    ("venus", 2, "Venus system barycenter"),
    ("earth", 399, "Earth geocenter (via Earth-Moon barycenter 3)"),
    ("mars", 4, "Mars system barycenter (Mars center 499 unavailable)"),
    ("jupiter", 5, "Jupiter system barycenter (center 599 unavailable)"),
    ("saturn", 6, "Saturn system barycenter (center 699 unavailable)"),
    ("uranus", 7, "Uranus system barycenter (center 799 unavailable)"),
    ("neptune", 8, "Neptune system barycenter (center 899 unavailable)"),
};
Console.WriteLine("--- Supported planetary objects (SN4) ---");
foreach (var (id, naif, label) in sn4Planets)
    Console.WriteLine($"  {id,-10} NAIF {naif,-4} {label}  supported={indexed.IsBodySupported(id)}");
Console.WriteLine($"  {"mercury_center",-10} NAIF {199,-4} Mercury body center (coincident with barycenter 1)");
Console.WriteLine($"  {"venus_center",-10} NAIF {299,-4} Venus body center (coincident with barycenter 2)");
Console.WriteLine();

// Off-grid epoch query (CLI args [bodyId] [jdTdb] select target/epoch).
string sn4Target = args.Length > 0 ? args[0] : "saturn";
if (!indexed.IsBodySupported(sn4Target))
{
    Console.WriteLine($"Body '{sn4Target}' is not in the SN4 dataset (Pluto and planet centers 499/599/699/799/899 are unsupported).");
    Console.WriteLine($"Supported: {string.Join(", ", indexed.SupportedBodies)}");
    return;
}
var sn4Epoch = new AstroTime(jdTdb, TimeScale.Tdb);
Console.WriteLine($"Requested body: {sn4Target}, TDB epoch: JD {jdTdb:F7}");
var sn4State = indexed.GetState(sn4Target, sn4Epoch, barycentric);
if (sn4State.IsSuccess && sn4State.Value is var (spos, svel))
{
    Console.WriteLine($"{sn4Target} position (km): ({spos.X / 1000.0:F3}, {spos.Y / 1000.0:F3}, {spos.Z / 1000.0:F3})");
    Console.WriteLine($"{sn4Target} velocity (km/s): ({svel.X / 1000.0:F6}, {svel.Y / 1000.0:F6}, {svel.Z / 1000.0:F6})");
    Console.WriteLine($"Source: {sn4State.Provenance}  Quality: {sn4State.Status}");
    Console.WriteLine($"Frame: {sn4State.Frame}  Origin: SSB  Units: m, m/s (evaluated in km, converted once on output)");

    string sn4Other = sn4Target == "earth" ? "saturn" : "earth";
    var sn4RelCalc = new RelativePositionCalculator(indexed);
    var sn4Rel = sn4RelCalc.Calculate(sn4Target, sn4Other, sn4Epoch);
    if (sn4Rel.IsSuccess)
        Console.WriteLine($"{sn4Target}-{sn4Other} distance: {sn4Rel.Value!.SeparationDistanceKm:F3} km [{sn4Rel.Value.Status}]");
    else
        Console.WriteLine($"Distance unavailable: {sn4Rel.ErrorMessage}");
    Console.WriteLine($"Cache after queries: {indexed.CachedRecordCount} records, {indexed.CachedBytes} bytes " +
        $"(hits={indexed.CacheHitCount}, misses={indexed.CacheMissCount})");
}
else
{
    Console.WriteLine($"UNAVAILABLE [{sn4State.Status}]: {sn4State.ErrorMessage}");
}

Console.WriteLine();
Console.WriteLine("Stellar Navigator SN4 - Eight-planet offline evaluation confirmed.");
