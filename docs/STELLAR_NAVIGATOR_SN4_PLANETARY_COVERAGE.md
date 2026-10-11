# Stellar Navigator — Phase SN4

## Complete Solar System Coverage & Memory-Efficient Ephemerides

**Status:** Complete
**Date:** 2026-10-11
**Outcome:** A (Full Acceptance)
**Prior phase:** SN3 (Outcome A) — preserved in full.
**Branch:** `STELLAR_NAVIGATOR` (starting HEAD `dc7a6ddd`, `.phase` SN3)

---

## 1. What SN4 changed

SN3 proved pure-managed, continuous DE440s evaluation for six body ids
with 0.12 mm worst-case agreement against CSPICE — at the cost of parsing
a 61.2 MB JSON file into ~26 MB of managed coefficient objects on every
startup. SN4 keeps that scientific engine byte-for-byte intact and adds:

1. **Complete eight-planet coverage** — Mercury (1), Venus (2), Saturn (6),
   Uranus (7), Neptune (8) system barycenters, extracted verbatim from the
   same authenticated `de440s.bsp` with the same validated pipeline.
2. **Genuine Mercury/Venus body-center segments** — NAIF 199/299 exported
   under explicit `mercury_center` / `venus_center` labels (measured
   coincident with their barycenters under DE440s conventions; §4).
3. **A versioned indexed binary coefficient format (SNEPH04B v1)** —
   32.1 MB for 106,177 records (SN3 JSON: 61.2 MB for 75,350 records).
4. **A bounded-memory lazy-loading provider** —
   `IndexedChebyshevEphemerisProvider`: index-only startup (~0 MiB
   retained), on-demand record reads, 4 MiB default LRU cache, bit-exact
   agreement with the SN3 provider on shared bodies.
5. **523 new independent CSPICE comparisons** (400 off-grid, ≥50 per new
   planet) plus lazy-loader equivalence, corruption, boundary, and
   application-integration tests: **135/135 automated tests pass**
   (92 SN1–SN3 preserved, 43 SN4).

Scientific correctness was never traded for performance: the Clenshaw
evaluator, normalization, chain-rule velocity scaling, center chaining,
TDB handling, J2000 frame, and km-to-m conversion are unchanged, and the
SN3 JSON dataset + provider are frozen as the regression gate.

---

## 2. DE440s source provenance (unchanged, re-authenticated)

| Item | Value |
|------|-------|
| Kernel | `de440s.bsp` — JPL DE440s small-body/planet ephemeris |
| Source URL | https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp |
| Checksum list | `aa_checksums.txt` (same directory) |
| Retrieval date | 2026-10-11 |
| Published MD5 | `3917ee56769db332790c751e2168843d` |
| Verified local MD5 | `3917ee56769db332790c751e2168843d` — **match** |
| Published SHA-256 | `c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2` |
| Verified local SHA-256 | same — **match** |
| Leapseconds kernel (host verification only) | `naif0012.tls` |
| Host tooling | Python 3.12 + spiceypy 8.2.0 (CSPICE N0067) + numpy 2.5.4 |

---

## 3. Supported NAIF object identifiers

Runtime body id → NAIF target (center in parentheses):

| Runtime id | NAIF | Center | Meaning |
|---|---|---|---|
| `sun` | 10 (0) | SSB | Sun |
| `mercury` | 1 (0) | SSB | **Mercury system barycenter** — NEW |
| `venus` | 2 (0) | SSB | **Venus system barycenter** — NEW |
| `emb` | 3 (0) | SSB | Earth-Moon barycenter (chain helper) |
| `earth` | 399 (3) | EMB | Earth geocenter |
| `moon` | 301 (3) | EMB | Moon |
| `mars` | 4 (0) | SSB | **Mars system barycenter** (center 499 absent) |
| `jupiter` | 5 (0) | SSB | **Jupiter system barycenter** (center 599 absent) |
| `saturn` | 6 (0) | SSB | **Saturn system barycenter** — NEW (center 699 absent) |
| `uranus` | 7 (0) | SSB | **Uranus system barycenter** — NEW (center 799 absent) |
| `neptune` | 8 (0) | SSB | **Neptune system barycenter** — NEW (center 899 absent) |
| `mercury_center` | 199 (1) | Mercury barycenter | **Mercury body center** — NEW, coincident with 1 |
| `venus_center` | 299 (2) | Venus barycenter | **Venus body center** — NEW, coincident with 2 |
| `ssb` | 0 | — | Solar System barycenter (zero state) |

Every new planetary segment was inspected in the kernel descriptor table
and confirmed **directly SSB-centered** (center 0) — no assumed
relationships. The provider re-validates all center chains at load
(resolvable ancestors, acyclicity); missing ancestors or cycles are load
errors, never silent fabrications.

---

## 4. Center versus barycenter distinctions

- Planetary barycenter ids (1, 2, 4, 5, 6, 7, 8) are **system**
  barycenters, never relabeled as physical planet centers
  (199, 299, 499, 599, 699, 799, 899).
- Mars (4), Jupiter (5), Saturn (6), Uranus (7), Neptune (8) centers are
  **absent from `de440s.bsp`** (verified: no 499/599/699/799/899 segments
  exist). Queries for them report `InsufficientInformation`; no
  barycenter-for-center substitution is performed or implied. The old
  "~20 cm Mars offset" claim remains retired (see SN3 §12).
- Mercury center (199) and Venus center (299) **do exist** as genuine
  single-record degree-1 Type-2 segments centered on their barycenters
  (199→1, 299→2). Independent `spkezr` measurement shows both are
  **identically zero relative to their barycenters at all epochs**
  (e.g. `spkezr('199', et, 'J2000', 'NONE', '1')` = exactly
  `[0,0,0,0,0,0]` for et ∈ {0, 1e9, −4e9} s): under the applicable NAIF
  conventions the inner-planet barycenter and body center-of-mass
  positions coincide. The runtime exports them under explicit
  `mercury_center` / `venus_center` labels so applications can
  distinguish the identifiers while observing the measured coincidence
  (tested: zero offset, bit-exact).
- The SN1 catalog's `referenceIds` (e.g. Mercury → `NAIF:199`) denote
  physical centers, while ephemeris ids `mercury`/`venus` denote
  barycenters (1/2) — the console app documents this convention clash
  rather than hiding it.

---

## 5. Added segment metadata (all Type 2, frame 1 = J2000)

| Body | Target | Degree | Record interval | Records | Coverage (JD TDB) |
|---|---|---|---|---|---|
| mercury | 1 | 13 | 8 d (691,200 s) | 13,700 | 2396752.5 – 2506352.5 |
| venus | 2 | 9 | 16 d (1,382,400 s) | 6,850 | same |
| saturn | 6 | 6 | 32 d (2,764,800 s) | 3,425 | same |
| uranus | 7 | 5 | 32 d | 3,425 | same |
| neptune | 8 | 5 | 32 d | 3,425 | same |
| mercury_center | 199 | 1 | whole span (9,469,440,000 s) | 1 | same |
| venus_center | 299 | 1 | whole span | 1 | same |

Preserved SN3 segments (bit-identical, §7): sun, emb, moon, earth, mars,
jupiter — 75,350 records. Dataset totals: **13 segments, 106,177 records,
4,014,116 binary64 words**.

---

## 6. Dataset layout — SNEPH04B version 1

File: `Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`
(32,117,373 bytes). All integers little-endian; all floats binary64 LE.

```text
Header:
  magic[8]              = "SNEPH04B"
  version u32           = 1
  segmentCount u32      = 13
  totalRecords u64      = 106177
  provenanceLength u32  + provenance UTF-8 JSON (source, URLs, dates,
                          kernel MD5/SHA-256, frame, origin, time scale,
                          units, center chains, body notes, generation tool)
Segment table (ascending target id):
  bodyId, centerBodyId  (u32 length + UTF-8)
  targetId, centerId, frameId, segmentType, degree (i32)
  recordIntervalSeconds, initEt (f64)
  recordCount (u64)
  coverageStartJd, coverageEndJd (f64)
  dataOffset (u64, absolute), recordLength (u64 doubles = RSIZE)
Coefficient data: contiguous f64 records per segment, each
  [MID, RADIUS, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD] verbatim, km/km/s-native.
```

Why binary instead of JSON: coefficient doubles are stored at 8 bytes
each (vs ~20 chars in JSON), loading the index touches kilobytes, and no
per-coefficient managed objects are ever allocated on the optimized path.
The SN3 JSON dataset is **preserved unchanged** (same file, same SHA-256,
same provider constant) as the compatibility/regression gate — SN4 does
not migrate or reinterpret it.

Integrity model (source-anchored, as SN3):

- SHA-256 is computed over the distributed bytes and verified against
  the trusted digest embedded in the provider
  (`IndexedChebyshevEphemerisProvider.ExpectedDatasetSha256` =
  `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`)
  — never against a sidecar hash.
- Header, version, segment/degree/record counts, and every data offset
  are validated with overflow-checked arithmetic before any record
  access; truncated, overlapping, or malformed files fail at load with
  `InvalidDataException`. Unsupported versions are rejected (no silent
  byte-swapped reinterpretation; big-endian hosts are refused
  explicitly).
- Each freshly read record is validated (finite MID/RADIUS, positive
  radius, finite coefficients, u ∈ [−1,1] within slack); failures at
  query time return explicit `InvalidInput` diagnostics, never fabricated
  states. Cached copies are verbatim and never re-validated into
  different results.
- Trust assumption (documented): verification happens at open. The
  provider holds an open read handle; serving a replaced file requires a
  new provider instance, which re-verifies.

---

## 7. Generation and verification process (deterministic, reproducible)

Host tool `Apps/StellarNavigator/tools/sn4_extract_chebyshev.py`
(same authenticated pipeline as SN3, extended target set):

1. Authenticate the kernel (MD5 + SHA-256; abort otherwise).
2. Inventory **all** 14 descriptors (printed; Pluto 9 explicitly skipped).
3. Require Type 2 + frame 1 for every exported target; verify trailer
   (`size = N·RSIZE + 4`, `RSIZE = 3·(PD+1)+2`).
4. Verify every record's stored MID/RADIUS against
   `INIT+(i+0.5)·INTLEN` / `INTLEN/2` — observed deviation **exactly 0**.
5. Spot-check raw-record Clenshaw evaluation against independent `spkezr`
   (target relative to its own center) at first/mid/last records
   (tolerance 1e-6 km).
6. Require every non-SSB center to be exported (chain resolvability).
7. Optional `--verify-against`: bit-exact cross-check of the 6 shared
   segments against the frozen SN3 JSON — **all 75,350 records
   bit-identical** (proves the new pipeline reproduces SN3 data exactly).
8. Write the binary file; re-read every record and compare bit-exactly;
   print kernel + output digests for the manifest.

Output digest: `d52b9157…96d2c0a` (full value in §6).

---

## 8. Memory-loading architecture

`IndexedChebyshevEphemerisProvider` (implements `IEphemerisProvider`,
`IDisposable`):

- **Startup:** hash file → parse provenance + 13-row segment index
  (kilobytes). Zero coefficient records materialized (`CachedBytes = 0`
  after load, asserted by test).
- **Query:** `floor((et−INIT)/INTLEN)` selects the record per chain level
  (O(1) index arithmetic, final-record clamping identical to SN3) →
  record fetched from the bounded LRU cache or read via one positioned
  file read → evaluated with the **unchanged** `ChebyshevEvaluator` →
  chained → single km-to-m conversion on output.
- **Cache:** keyed by (segment, record index); default budget **4 MiB**,
  configurable (including 0 = caching disabled, still correct). LRU
  eviction is deterministic; oversized single records bypass the cache
  without error. `CachedBytes ≤ budget` is asserted under random-query,
  tiny-budget, and concurrency tests. Cache statistics
  (`CacheHitCount/MissCount`, `CachedRecordCount`, `CachedBytes`,
  `ClearCache()`) are exposed for diagnostics.
- **Ownership/concurrency:** the provider owns one open read handle
  (released by `Dispose()`; post-dispose queries throw
  `ObjectDisposedException`). Concurrent reads are safe via a single
  internal lock over the stream + cache; record copies are immutable to
  callers, and evaluation is pure computation. Reopening the same file
  (new instance) re-verifies and agrees bit-exactly (tested).
- A cache **cannot** alter scientific results: it stores verbatim
  coefficient copies; 660 bit-exact lazy-vs-SN3 comparisons plus
  repeat-query-under-pressure tests prove cache-history independence.

---

## 9. Scientific comparison results

Independent fixture `StellarNavigator.Tests/sn4_reference.json` (523
cases, seed 20261012) generated by `tools/sn4_make_reference.py` using
**only** `spkezr` (JD-quantized epochs, same honest-epoch methodology as
SN3): J2000 states, **400 deterministic off-grid random epochs (50 per
new planet SSB-relative + Earth-relative + center cases)**, first/last
coverage epochs, 81 record-boundary cases (exact 8/16/32-day junctions ±
{0, 1 ms, 0.5 s}; center mid-span probes), Earth-relative pairs.

| Metric | Value |
|---|---|
| New comparisons | **523 (400 off-grid; ≥250 required ✓)** |
| Maximum position error | **9.537e-7 km (0.95 mm)** — Uranus-vs-Earth at end of coverage |
| RMS position error | 1.937e-7 km |
| Maximum velocity error | **1.421e-14 km/s** |
| RMS velocity error | 2.676e-15 km/s |
| Boundary-case max errors | 4.768e-7 km / 7.105e-15 km/s (81 cases, zero failures) |
| Earth-relative max errors | 9.537e-7 km / 1.421e-14 km/s (65 cases) |
| Inherited gates | 1e-6 km / 1e-9 km/s — **satisfied, no tolerance change** |

The worst case sits close to the position gate: expected physics, not a
defect — outer-planet segments use low-degree polynomials (5–6) over
long (32-day) intervals at large heliocentric distances, and
Earth-relative states sum two evaluation errors. Margin on velocity is
~1e5×. Gates were not weakened; the result is reported as-is.

Lazy-loader equivalence (identical datasets/epochs):

| Check | Result |
|---|---|
| Bit-exact lazy-vs-SN3, deterministic epochs (6 shared bodies) | 60/60 exact |
| Bit-exact lazy-vs-SN3, 600 random epochs | 600/600 exact |
| 300 alternating-target queries under 2 KiB budget | exact, budget held |
| Retained SN3 fixture (267 cases) re-run through lazy path | max 1.192e-7 km — **matches SN3's reported maximum exactly** |

What these numbers mean (kept distinct): **source accuracy** is JPL's
(DE440s fit quality — not measured here); **numerical agreement** is what
the tables above report (managed evaluator vs CSPICE, sub-mm); neither
constitutes **navigational reliability** — this remains research and
simulation software, not certified navigation equipment.

---

## 10. Performance measurements (host, reproducible)

Benchmark: `StellarNavigator.Benchmarks`
(`dotnet run --project StellarNavigator.Benchmarks -c Release`),
fixed seed 20261014, 20,000 random queries spanning full coverage.
Release / .NET 8.0.31 desktop host — **host figures only, not
bare-metal guideXOS budgets**.

| Measurement | SN3 | SN4 |
|---|---|---|
| Dataset size | 61,163,371 B | 32,117,373 B |
| Records | 75,350 | 106,177 |
| Startup/index load time | 1.18 s | 0.11 s |
| Retained heap after load | 25.3 MiB | ~0.0 MiB (index only, +0 B coefficients) |
| Process working set after load | 241.5 MiB | 69.0 MiB |
| Cache budget | unbounded/full load | 4.0 MiB default (configurable, 0 allowed) |
| Cold-pass throughput (20k random) | 421,089 eval/s | 103,417 eval/s |
| Warm-pass throughput (20k repeated) | 660,295 eval/s | 106,735 eval/s |
| Cold-pass peak heap above post-load | (in load figure) | +12.0 MiB transient during 20k queries |
| Cold-pass cache | n/a | hits=6,730 misses=19,437 retained=4,194,224 B (≤ budget ✓) |
| Tight-budget (64 KiB) throughput | n/a | 100,571 eval/s (miss-dominated, still correct) |

Reading the table honestly: SN4 trades per-evaluation speed (~10 µs vs
~2 µs — the price of on-demand file reads) for ~11× faster startup,
~zero retained coefficients, a hard 4 MiB cache bound, and half the
file size while serving 41% more records. A 300-year random-epoch
working set exceeds 4 MiB, so random benchmarks are miss-dominated;
sequential/repeated access is hit-dominated (unit tests demonstrate
hits, deterministic repeatability, and exact restart agreement).
No benchmark used only cached records to characterize typical cost —
cold and warm passes are reported separately.

SN3 baseline figures above were measured in this phase on the same host
under comparable conditions (they agree with the SN3 doc's 1.85 s /
26.5 MB within machine variance).

---

## 11. Offline operation and application integration

- Runtime has zero network/native/Python dependencies (unchanged); the
  console app runs fully offline against the two shipped datasets.
- Missing SN4 file → explicit "SN4 dataset missing" diagnostic; SN2/SN3
  sections remain available and distinctly labeled.
- The app lists all eight planets with exact NAIF ids and
  barycenter/center labels, accepts `[bodyId] [jdTdb]` arguments,
  prints position/velocity vectors, Earth-relative distance, frame
  (J2000/Barycentric), origin (SSB), units, provenance, quality, and
  cache statistics. Demonstrated offline: Saturn @ JD 2460000.5
  (1,615,967,540.907 km from Earth), Neptune @ J2000
  (4,641,196,276.143 km from Earth); `pluto` yields an explicit
  unsupported-body diagnostic listing all 14 supported ids.
- Requesting a missing dataset never downloads anything; failures are
  typed (`StaleData`, `InsufficientInformation`,
  `UnsupportedCalculation`, `InvalidInput`) and deterministic.

---

## 12. Testing and regression (135/135, 0 new warnings)

- All **92 SN1–SN3 tests pass unmodified** (SN3 JSON/provider frozen).
- **43 new SN4 tests**: 12 binary-format (index, bounds, versions,
  truncation, minimal-file round-trip), 19 indexed-provider (8-planet
  support, coverage, failure modes, determinism, disposal, reopen,
  concurrency, tiny/zero budgets, Mercury/Venus coincidence,
  application-path via `RelativePositionCalculator`), 4 CSPICE
  verification (523 cases + boundary + Earth-relative + ≥50/body gate),
  4 lazy-equivalence (660 bit-exact + 267-case SN3-fixture rerun), plus
  shared path helpers.
- Corruption/failure tests: bad magic/version, truncated header/data,
  degree-length mismatch, half-truncated dataset, checksum mismatch,
  out-of-coverage both sides, unknown bodies, unsupported frames, NaN
  epochs, disposed-provider behavior.
- Build: 0 new warnings, 0 errors (the single `xUnit2002` notice in
  SN2 `EphemerisTests.cs` predates SN4 and is untouched).

---

## 13. Remaining limitations / unsupported objects

- Pluto barycenter (9) exists in DE440s but is out of scope (not a major
  planet) — explicitly unsupported, documented in dataset provenance.
- Planet centers 499/599/699/799/899 are absent from DE440s — a future
  phase needs an additional kernel (e.g. full DE440 or satellite
  ephemerides) to support true planet-center queries.
- Geometric states only; no light-time/stellar aberration, no frame
  engine, no leap-second UTC↔TDB system (approximate SN2 drift model
  retained for UTC inputs; TDB recommended).
- JD-double epoch API quantizes input time to ~80 µs near 2.5e6 (API
  property, factored out of comparisons honestly — see SN3 §6).
- `ValidatedReference` = CSPICE agreement, not flight certification.
- Console UI only; exclusions from the mission (§16) respected — no new
  frameworks, kernels, or speculative systems.

---

## 14. Recommended SN5 scope

1. Resolve a 499-capable kernel to settle the true Mars
   barycenter-vs-center offset and support planet-center queries with
   explicit labeling (same pattern as 199/299 here).
2. Consider Pluto (9) only if dwarf-planet scope is explicitly requested.
3. Leap-second-table-based UTC↔TDB conversion from a bundled LSK-derived
   table.
4. Frame transformations (barycentric ↔ heliocentric ↔ planet-centered).
5. Evaluate memory-mapped record access if guideXOS-native budgets need
   lower per-miss latency than FileStream reads.
6. Visualization only after numerical correctness (locked since SN3).

---

## 15. Safety

Research and simulation software — not qualified navigation equipment.
All JPL-derived results carry source/frame/origin/epoch metadata;
simulated spacecraft data is labeled as such. Evaluation is supported
**within verified ephemeris coverage only** — never extrapolated, never
certified for spacecraft navigation.
