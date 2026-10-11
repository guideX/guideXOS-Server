using System.Diagnostics;
using Astronomy.Core;
using Astronomy.Ephemeris;
using Navigation.Core;

// Stellar Navigator SN4 — reproducible performance benchmark (host only).
//
// Compares the SN3 full-load JSON provider against the SN4 indexed
// lazy-loading provider under comparable host conditions:
//   dotnet run --project StellarNavigator.Benchmarks -c Release [-- <queries>]
//
// All figures are HOST measurements (desktop .NET GC + working set); they
// must NOT be quoted as bare-metal guideXOS memory performance. The benchmark
// uses a fixed seed so query streams are reproducible. Random requests span
// the complete time coverage and all supported bodies; cold-start (fresh
// provider, empty cache) and warm-cache passes are measured separately.

int queryCount = args.Length > 0 && int.TryParse(args[0], out var n) && n > 0 ? n : 20000;
const double CoverageStartJd = 2396752.5;
const double CoverageEndJd = 2506352.5;
string[] sn3Bodies = { "sun", "emb", "moon", "earth", "mars", "jupiter" };
string[] sn4Bodies =
{
    "sun", "emb", "moon", "earth", "mars", "jupiter",
    "mercury", "venus", "saturn", "uranus", "neptune",
    "mercury_center", "venus_center",
};

string dataDir = Path.Combine(AppContext.BaseDirectory, "Data");
string jsonPath = Path.Combine(dataDir, "ephemeris_de440s_chebyshev.json");
string binPath = Path.Combine(dataDir, "ephemeris_de440s_chebyshev.sn4bin");

long HeapBytes()
{
    GC.Collect(2, GCCollectionMode.Forced, blocking: true);
    GC.WaitForPendingFinalizers();
    GC.Collect(2, GCCollectionMode.Forced, blocking: true);
    return GC.GetTotalMemory(forceFullCollection: false);
}

long WorkingSet() => Process.GetCurrentProcess().WorkingSet64;

Console.WriteLine("# Stellar Navigator SN4 benchmark (host measurements)");
Console.WriteLine($"# queries per pass: {queryCount}, seed: 20261014");
Console.WriteLine($"# runtime: {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription}");
Console.WriteLine();

// ─── SN3 baseline ───
long heapBefore = HeapBytes();
var sw = Stopwatch.StartNew();
var sn3 = new ChebyshevEphemerisProvider(jsonPath);
double sn3LoadSec = sw.Elapsed.TotalSeconds;
long sn3HeapAfter = HeapBytes();
long sn3Retained = sn3HeapAfter - heapBefore;
long sn3WorkingSet = WorkingSet();

var sn3Plan = MakePlan(queryCount, 20261014, sn3Bodies);
var (sn3ColdSec, sn3ColdPeak) = RunQueries(sn3, sn3Plan);
var (sn3WarmSec, _) = RunQueries(sn3, sn3Plan);

// ─── SN4 indexed ───
heapBefore = HeapBytes();
sw.Restart();
using var sn4 = new IndexedChebyshevEphemerisProvider(binPath);
double sn4LoadSec = sw.Elapsed.TotalSeconds;
long sn4HeapAfter = HeapBytes();
long sn4Retained = sn4HeapAfter - heapBefore;
long sn4WorkingSet = WorkingSet();
long sn4IndexCached = sn4.CachedBytes;

var sn4Plan = MakePlan(queryCount, 20261014, sn4Bodies);
var (sn4ColdSec, sn4ColdPeak) = RunQueries(sn4, sn4Plan);
long sn4ColdHits = sn4.CacheHitCount;
long sn4ColdMisses = sn4.CacheMissCount;
long sn4ColdBytes = sn4.CachedBytes;
var (sn4WarmSec, _) = RunQueries(sn4, sn4Plan);
long sn4WarmHits = sn4.CacheHitCount - sn4ColdHits;

// Fresh provider, tiny budget: miss-dominated behavior.
using var sn4Tight = new IndexedChebyshevEphemerisProvider(binPath, cacheBudgetBytes: 65536);
var (sn4TightSec, _) = RunQueries(sn4Tight, sn4Plan);

Console.WriteLine("| Measurement | SN3 (full-load JSON) | SN4 (indexed lazy) |");
Console.WriteLine("|---|---|---|");
Console.WriteLine($"| Dataset size | {new FileInfo(jsonPath).Length} bytes | {new FileInfo(binPath).Length} bytes |");
Console.WriteLine($"| Records | {sn3.TotalRecords} | {sn4.TotalRecords} |");
Console.WriteLine($"| Startup/index load time | {sn3LoadSec:F2} s | {sn4LoadSec:F2} s |");
Console.WriteLine($"| Retained heap after load | {sn3Retained / 1048576.0:F1} MiB | {sn4Retained / 1048576.0:F1} MiB (+{sn4IndexCached} B coefficients) |");
Console.WriteLine($"| Process working set after load | {sn3WorkingSet / 1048576.0:F1} MiB | {sn4WorkingSet / 1048576.0:F1} MiB |");
Console.WriteLine($"| Cache budget | unbounded/full load | {sn4.CacheBudgetBytes / 1048576.0:F1} MiB (default) |");
Console.WriteLine($"| Cold-pass throughput ({queryCount} random queries) | {queryCount / sn3ColdSec:F0} eval/s | {queryCount / sn4ColdSec:F0} eval/s |");
Console.WriteLine($"| Warm-pass throughput ({queryCount} repeated queries) | {queryCount / sn3WarmSec:F0} eval/s | {queryCount / sn4WarmSec:F0} eval/s (cache hits={sn4WarmHits}) |");
Console.WriteLine($"| Cold-pass peak managed heap | included above | {(sn4ColdPeak - sn4HeapAfter) / 1024.0:F0} KiB above post-load |");
Console.WriteLine($"| Cold-pass cache | n/a | hits={sn4ColdHits} misses={sn4ColdMisses} retained={sn4ColdBytes} B |");
Console.WriteLine($"| Tight-budget (64 KiB) throughput | n/a | {queryCount / sn4TightSec:F0} eval/s (miss-dominated) |");
Console.WriteLine();
Console.WriteLine("Notes:");
Console.WriteLine("- Cold pass uses a fresh provider (SN4 cache starts empty); warm pass repeats");
Console.WriteLine("  the identical query stream, so warm figures are cache-dominated.");
Console.WriteLine("- Random queries span JD 2396752.5-2506352.5 across all supported bodies");
Console.WriteLine("  (SN3: 6 bodies; SN4: 13 ids incl. new planets and Mercury/Venus centers).");
Console.WriteLine("- Host desktop .NET figures only; not bare-metal guideXOS budgets.");

List<(string Body, double Jd)> MakePlan(int count, int seed, string[] bodies)
{
    var rng = new Random(seed);
    var plan = new List<(string, double)>(count);
    for (int i = 0; i < count; i++)
        plan.Add((bodies[rng.Next(bodies.Length)],
            CoverageStartJd + rng.NextDouble() * (CoverageEndJd - CoverageStartJd)));
    return plan;
}

(double Seconds, long PeakHeap) RunQueries(
    IEphemerisProvider provider, List<(string Body, double Jd)> plan)
{
    var frame = ReferenceFrame.Barycentric;
    long peak = 0;
    var timer = Stopwatch.StartNew();
    for (int i = 0; i < plan.Count; i++)
    {
        var (body, jd) = plan[i];
        var result = provider.GetState(body, new AstroTime(jd, TimeScale.Tdb), frame);
        if (!result.IsSuccess)
            throw new InvalidOperationException($"Benchmark query failed: {body} jd={jd}: {result.ErrorMessage}");
        if ((i & 1023) == 1023)
            peak = Math.Max(peak, GC.GetTotalMemory(forceFullCollection: false));
    }
    timer.Stop();
    peak = Math.Max(peak, GC.GetTotalMemory(forceFullCollection: false));
    return (timer.Elapsed.TotalSeconds, peak);
}
