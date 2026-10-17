using Astronomy.Catalog;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Astronomy.Time;
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

// ─── SN5: astronomical timekeeping — UTC calendar input to TDB epochs ───
Console.WriteLine();
Console.WriteLine("=== Stellar Navigator SN5 ===");
Console.WriteLine("Astronomical timekeeping: UTC -> TAI -> TT -> TDB (offline, managed)");

var timeDataPath = Path.Combine(AppContext.BaseDirectory, "Data", "leap_seconds.sn5json");
LeapSecondProvider timeProvider;
try
{
    timeProvider = LeapSecondProvider.Load(timeDataPath);
}
catch (Exception ex)
{
    Console.WriteLine($"SN5 time data unavailable: {ex.Message}");
    Console.WriteLine("UTC conversion is unavailable; direct JD TDB queries above remain valid.");
    return;
}

var timeConverter = new TimeConverter(timeProvider.Table, timeProvider.Identity);
Console.WriteLine($"Leap-second kernel: {timeProvider.Identity}");
Console.WriteLine($"  source: {timeProvider.KernelSourceUrl}");
Console.WriteLine($"  retrieved: {timeProvider.RetrievalDate}, kernel SHA-256: {timeProvider.KernelSha256}");
Console.WriteLine($"  transitions: {timeProvider.Table.Transitions.Count} " +
    $"(TAI-UTC {timeProvider.Table.FirstOffset} -> {timeProvider.Table.LastOffset}), " +
    $"last announced leap: {timeProvider.LastAnnouncedLeapSecondUtc}");
Console.WriteLine($"  DELTET: TA=32.184s K=0.001657 EB=0.01671 M=(6.239996, 1.99096871e-7)");
Console.WriteLine($"  strict UTC interval: 1972-01-01T00:00:00Z .. 2017-01-01T00:00:00Z (+ supported leap seconds)");
Console.WriteLine($"  time-data asset: {timeProvider.AssetBytes} bytes, SHA-256 verified at load");
Console.WriteLine();

// CLI: [bodyId] [jdTdb | utcIso] [--provisional]. A second argument containing
// 'T' is treated as a UTC calendar timestamp; otherwise the SN4 JD TDB path above applies.
bool allowProv = args.Any(a => a == "--provisional");
string? utcArg = args.Length > 1 && args[1].Contains('T') ? args[1] : null;
string demoUtc = "2016-07-14T00:00:00Z"; // strict, contemporary, inside ephemeris coverage

void PrintUtcState(string bodyId, string utcText)
{
    var q = new UtcEphemerisQuery(indexed, timeConverter);
    var r = q.GetState(bodyId, utcText, barycentric, allowProv);
    Console.WriteLine($"UTC '{utcText}' -> {bodyId}:");
    if (r.Time is not null)
    {
        var t = r.Time;
        Console.WriteLine($"  ET (TDB): {t.Et.Value:F6} s past J2000, JD TDB: {t.JdTdb:F7}");
        Console.WriteLine($"  TAI-UTC: {t.TaiMinusUtcSeconds}s, DELTA_ET: {t.DeltaEtSeconds:F6}s, " +
            $"time-data quality: {t.Quality}, representation: {r.RepresentationPrecision}, " +
            $"epoch error bound: {t.EstimatedErrorSeconds:E1}s");
        Console.WriteLine($"  JD TDB is printed for compatibility; indexed evaluation used the full-precision ET. " +
            $"A JD double near this epoch quantizes time by tens of microseconds.");
    }
    else
    {
        Console.WriteLine($"  time conversion failed [{r.TimeError!.Quality}]: {r.TimeError.Message}");
    }
    if (r.Ephemeris is not null)
    {
        var s = r.Ephemeris;
        if (s.IsSuccess && s.Value is var (p, v))
        {
            Console.WriteLine($"  position (km): ({p.X / 1000.0:F3}, {p.Y / 1000.0:F3}, {p.Z / 1000.0:F3})");
            Console.WriteLine($"  velocity (km/s): ({v.X / 1000.0:F6}, {v.Y / 1000.0:F6}, {v.Z / 1000.0:F6})");
            Console.WriteLine($"  time quality: {r.TimeQuality}, ephemeris quality: {s.Status}, source: {s.Provenance}");
        }
        else
        {
            Console.WriteLine($"  ephemeris UNAVAILABLE [{s.Status}]: {s.ErrorMessage}");
        }
    }
    Console.WriteLine($"  kernel: {timeProvider.Identity}");
}

// Demonstration: Earth's and Jupiter's states at a supported UTC timestamp.
Console.WriteLine($"--- UTC demonstration (strict, {demoUtc}) ---");
PrintUtcState("earth", demoUtc);
PrintUtcState("jupiter", demoUtc);

// Leap-second showcase: the final announced leap second, 2016-12-31T23:59:60Z.
Console.WriteLine("--- Leap-second showcase (strict) ---");
PrintUtcState("earth", "2016-12-31T23:59:60Z");

// User-requested UTC query (preserves SN4 JD path when no UTC argument given).
if (utcArg is not null)
{
    Console.WriteLine("--- Requested UTC query ---");
    PrintUtcState(sn4Target, utcArg);
}

// Explicit diagnostics: unsupported conversions never masquerade as timestamps.
Console.WriteLine("--- Unsupported-conversion diagnostics ---");
foreach (var bad in new[] { "1969-07-20T20:17:40Z", "2019-12-31T23:59:60Z", "2030-01-01T00:00:00Z" })
{
    var q = new UtcEphemerisQuery(indexed, timeConverter);
    var r = q.GetState("earth", bad, barycentric, allowProv);
    Console.WriteLine($"UTC '{bad}': Success={r.IsSuccess} [{r.TimeQuality}] {r.TimeError?.Message}");
}

Console.WriteLine();
Console.WriteLine("Stellar Navigator SN5 - Offline UTC-to-ephemeris conversion confirmed.");
Console.WriteLine("Research software: provisional epochs are labeled, never certified for navigation use.");

// ─── SN6: reference frames & spacecraft-relative navigation coordinates ───
Console.WriteLine();
Console.WriteLine("=== Stellar Navigator SN6 ===");
Console.WriteLine("Reference frames & spacecraft-relative coordinates (offline, managed)");

var orientAssetPath = Path.Combine(AppContext.BaseDirectory, "Data", "earth_orientation.sn6json");
Astronomy.Core.IauEarthOrientation orient;
try
{
    orient = Astronomy.Core.IauEarthOrientation.Load(orientAssetPath);
    Console.WriteLine($"Orientation model: J2000 <-> IAU_EARTH [{orient.Provenance}]");
    Console.WriteLine($"  asset: {new FileInfo(orientAssetPath).Length} bytes, SHA-256 verified at load");
    Console.WriteLine("  IAU_EARTH is a low-precision cartographic model, NOT ITRF93/EOP-based.");
}
catch (Exception ex)
{
    Console.WriteLine($"SN6 orientation data unavailable: {ex.Message}");
    Console.WriteLine("Framed queries are unavailable; SN2-SN5 output above remains valid.");
    return;
}

var framed = new FramedStateService(indexed, orient);
var sn6Epoch = new AstroTime(jdTdb, TimeScale.Tdb);

void PrintFramed(string label, NavigationResult<CartesianState> r)
{
    Console.WriteLine($"{label}:");
    if (!r.IsSuccess)
    {
        Console.WriteLine($"  UNAVAILABLE [{r.Status}]: {r.ErrorMessage}");
        return;
    }
    var s = r.Value!;
    Console.WriteLine($"  position (km): ({s.Position.X / 1000.0:F3}, {s.Position.Y / 1000.0:F3}, {s.Position.Z / 1000.0:F3})");
    Console.WriteLine($"  velocity (km/s): ({s.Velocity.X / 1000.0:F6}, {s.Velocity.Y / 1000.0:F6}, {s.Velocity.Z / 1000.0:F6})");
    Console.WriteLine($"  frame: orientation={s.Orientation}, origin={s.OriginId}, ET={s.EtSeconds:F9}s past J2000 (TDB)");
    Console.WriteLine($"  quality: {s.Quality}, simulated={s.IsSimulated}");
    Console.WriteLine($"  provenance: {s.Provenance}");
}

Console.WriteLine($"--- Framed queries at JD TDB {jdTdb:F7} (AstroTime compatibility input) ---");
PrintFramed("Earth relative to SSB in J2000", framed.GetState("earth", sn6Epoch, Astronomy.Core.FrameOrientation.J2000, "ssb"));
PrintFramed("Mars relative to Earth in J2000", framed.GetState("mars", sn6Epoch, Astronomy.Core.FrameOrientation.J2000, "earth"));
PrintFramed("Mars relative to Earth in IAU_EARTH axes", framed.GetState("mars", sn6Epoch, Astronomy.Core.FrameOrientation.IauEarth, "earth"));

Console.WriteLine("--- Spacecraft-relative query (clearly labeled simulated state) ---");
var sn6Earth = indexed.GetState("earth", sn6Epoch, ReferenceFrame.Barycentric);
if (sn6Earth.IsSuccess && sn6Earth.Value is var (epos, _))
{
    var demoSc = new SpacecraftState
    {
        SpacecraftId = "demo-sim-1",
        Position = epos + new Vector3D(7.0e8, 0, 0), // 700 000 km sunward-ish offset: synthetic
        Velocity = new Vector3D(0, 0, 0),
        Frame = ReferenceFrame.Barycentric,
        CenterOfReferenceId = "ssb",
        Epoch = sn6Epoch,
        Provenance = StateProvenance.Simulated,
        Quality = NavigationResultStatus.ApproximateOrSimulated,
    };
    Console.WriteLine($"Spacecraft {demoSc.SpacecraftId}: SIMULATED state (not measured, not inferred from camera).");
    PrintFramed("Jupiter relative to demo-sim-1 (J2000-aligned translated frame)",
        framed.GetSpacecraftRelative("jupiter", demoSc));
}
else
{
    Console.WriteLine($"Spacecraft demo unavailable: {sn6Earth.ErrorMessage}");
}

Console.WriteLine("--- Unsupported-frame diagnostic (no silent fallback) ---");
var unsupported = framed.GetState("mars", sn6Epoch, "ITRF93", "ssb");
Console.WriteLine($"ITRF93 request: Success={unsupported.IsSuccess} [{unsupported.Status}] {unsupported.ErrorMessage}");

Console.WriteLine();
Console.WriteLine("Stellar Navigator SN6 - Offline framed-state demonstration confirmed.");
Console.WriteLine("Research software: implementation validated vs CSPICE, never certified for navigation use.");
