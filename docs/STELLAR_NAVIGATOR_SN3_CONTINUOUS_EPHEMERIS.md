# Stellar Navigator — Phase SN3

## Continuous JPL Ephemerides & Managed Chebyshev Evaluation

**Status:** Complete
**Date:** 2026-10-11
**Outcome:** A (Full Acceptance)
**Prior phase:** SN2 (Outcome A) — preserved in full.

---

## 1. Implementation strategy

**Selected: genuine Type-2 Chebyshev record extraction + pure-managed evaluator.**

Host-side Python + spiceypy (CSPICE N0067) reads the raw SPK Type-2
records verbatim from the already-authenticated `de440s.bsp` kernel and
writes a compact, self-contained JSON runtime dataset. The guideXOS runtime
evaluates those records with a small, fully tested, dependency-free managed
Chebyshev evaluator (Clenshaw recurrence + differentiated Clenshaw for
velocity). No DAF/SPK parsing, no CSPICE, no Python, and no network access
exist in the runtime path.

| Option | Evaluation |
|--------|------------|
| Native CSPICE interop | Rejected (same SN2 rationale: not assumable under guideXOS; adds validation surface with no offline benefit). |
| Managed DAF/SPK parser | Rejected as runtime scope: the runtime does not parse kernels at all — it evaluates pre-extracted records. The host extraction script uses only CSPICE DAF/descriptor APIs plus verbatim word reads. |
| Refitted polynomials | Rejected: coefficients are extracted verbatim (bit-exact), never refitted or resampled. |
| **Verbatim Type-2 export + managed evaluator** | **Selected.** Smallest justified scope; every numerical step validated against independent `spkezr` references. |

No complete SPICE toolkit, generic kernel parser, or unsupported SPK type
was implemented.

---

## 2. DE440s source information

| Item | Value |
|------|-------|
| Kernel | `de440s.bsp` — JPL DE440s small-body/planet ephemeris |
| Source URL | https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp |
| Checksum list | `aa_checksums.txt` (same directory) |
| Retrieval date | 2026-10-11 |
| Published MD5 | `3917ee56769db332790c751e2168843d` |
| Verified local MD5 | `3917ee56769db332790c751e2168843d` — **match** |
| SPK SHA-256 | `c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2` — **match** |
| Leapseconds kernel (host verification only) | `naif0012.tls` |
| Extraction/verification tooling (host only) | Python 3.12 + spiceypy 8.2.0 (CSPICE N0067) |
| Toolkit references | SPK Required Reading (`req/spk.html`), CSPICE N0067, JPL SSD ephemeris export pages |

---

## 3. Segment types used

All 14 segments in `de440s.bsp` are **SPK Type 2** (Chebyshev
position-only; frame id 1 = J2000). No Type 3 (separate velocity
polynomials) or other types are present, so no Type-3 evaluator was needed.
The runtime loader rejects any segment whose type is not 2 or whose frame
is not J2000 with an explicit diagnostic.

Exported segments (6 of 14; the SN2 supported set plus its chain helper):

| Body (runtime id) | Target | Center | Degree | Record interval | Records | Coverage (JD TDB) |
|---|---|---|---|---|---|---|
| sun | 10 | 0 (SSB) | 10 | 16 d (1,382,400 s) | 6,850 | 2396752.5 – 2506352.5 |
| emb (chain helper) | 3 | 0 (SSB) | 12 | 16 d | 6,850 | same |
| moon | 301 | 3 (EMB) | 12 | 4 d (345,600 s) | 27,400 | same |
| earth | 399 | 3 (EMB) | 12 | 4 d | 27,400 | same |
| mars (system barycenter) | 4 | 0 (SSB) | 10 | 32 d (2,764,800 s) | 3,425 | same |
| jupiter (system barycenter) | 5 | 0 (SSB) | 7 | 32 d | 3,425 | same |

Non-exported segments (1, 2, 6, 7, 8, 9, 199, 299) remain unsupported and
are reported as such; no states are fabricated for them. Bodies 199/299 are
single-record degree-1 segments (whole-interval) for Mercury/Venus centers.

---

## 4. Polynomial record format

Per the SPK Type-2 layout, each record is:

`[MID, RADIUS, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD]`

- `MID`: record midpoint, ET seconds past J2000.
- `RADIUS`: half-interval, seconds (= INTLEN/2).
- Coefficients in **km** (native kernel units).
- Record size RSIZE = 3·(PD+1)+2 (verified for every segment).
- Record `i` covers `[INIT + i·INTLEN, INIT + (i+1)·INTLEN]`.

The runtime dataset (`Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.json`,
61,163,371 bytes, 75,350 records, 2,976,325 doubles) stores these records
verbatim as JSON arrays. Python `repr` shortest-round-trip formatting plus a
post-write bit-exact re-read check prove **lossless** coefficient storage —
no precision was truncated.

| Dataset checksum | Value |
|---|---|
| File SHA-256 | `a26de29eecd7df8b349221e630328d8f45bdf588c65e916a6593b2e83c1a9dc2` |
| Trust anchor | Embedded as `ChebyshevEphemerisProvider.ExpectedDatasetSha256` (source constant + this document), verified on every load. A sidecar hash alone is never trusted. |

---

## 5. Coefficient extraction procedure

Host tool `Apps/StellarNavigator/tools/sn3_extract_chebyshev.py`
(deterministic: sorted segment order, fixed metadata, no wall-clock input):

1. Authenticate the kernel (MD5 + SHA-256 must match §2; abort otherwise).
2. Enumerate segment descriptors via `DAFBFS/DAFFNA/DAFGS/SPKUDS`; require
   Type 2 + frame 1 for every exported target.
3. Read each segment's trailer (`INIT, INTLEN, RSIZE, N`); verify
   `size = N·RSIZE + 4` and `RSIZE = 3·(PD+1)+2`.
4. Read all record words; verify every record's stored `MID/RADIUS` against
   `INIT+(i+0.5)·INTLEN` / `INTLEN/2` (observed deviation: exactly 0).
5. Spot-check raw-record Clenshaw evaluation against independent `spkezr`
   (target relative to its own center) at first/mid/last records
   (tolerance 1e-6 km; abort otherwise).
6. Write compact JSON; re-read and compare every double bit-exactly.
7. Print kernel + output digests for the manifest.

---

## 6. Time normalization, derivatives, units, frames

- Normalization: `u = (t − MID) / RADIUS`, valid on [−1, 1]. Record
  selection is `floor((t − INIT)/INTLEN)`, clamped so an epoch exactly at
  end-of-coverage deterministically selects the final record (no
  intermittent boundary failure; verified by 55 boundary test cases).
- Evaluation: Clenshaw recurrence with c₀ handled outside the loop (a
  prototype bug that folded c₀ into the loop caused ~6% errors and was
  caught by `spkezr` comparison before any production code was written).
- Velocity: differentiated Clenshaw gives d/du; chain-rule scaling
  `d/dt = (d/du)/RADIUS` yields km/s (coefficients km, radius s). There is
  no separate velocity polynomial (Type 2 by construction).
- Units: evaluation stays in km; a single final ×1000 gives SI meters and
  meters/second. No intermediate conversions.
- Frames/origin/time: J2000-compatible inertial axes (= ICRF axes for this
  purpose; both `Barycentric` and `IcrfJ2000` requests are served from the
  same geometric SSB-relative states, matching SN2 semantics; all other
  frames are rejected, never reinterpreted). Origin SSB (0). TDB epochs;
  `ET = (JD − 2451545.0)·86400`. Geometric states only (no light-time or
  stellar aberration).
- UTC inputs are mapped through the inherited SN2 linear drift model
  (`TdbTime.UtcToTdb`, approximate — leap-second-precise conversion remains
  out of scope); TDB input is recommended for full accuracy and is what all
  verification uses.
- Epoch-input quantization (documented API property): a JD double near
  2.5e6 resolves time to ~80 µs. Verification fixtures quantize every epoch
  through a JD double first, then evaluate CSPICE at the reconstructed ET —
  the exact epoch the provider reconstructs — so the comparison tests
  record selection, evaluation, and chaining rather than input formatting.

---

## 7. Center-chain composition

DE440s does **not** store the Moon or Earth directly relative to the SSB:
301 → 3 (EMB), 399 → 3 (EMB), 3 → 0 (SSB). (SN2's dataset stored the
already-chained SSB-relative vectors, so SN2 values remain correct; only
the "same origin" phrasing was imprecise — see §12.) The provider composes,
for compatible epochs and frames:

`state(target rel SSB) = state(target rel center) + state(center rel SSB)`

applied to position and velocity. Chain validity (resolvable ancestors,
acyclicity) is verified at load; missing ancestors, cycles, frame
mismatches, and per-level coverage gaps fail with explicit diagnostics.
Target 0 (SSB) short-circuits to the zero state within overall coverage.

---

## 8. Validation methodology and error statistics

Independent fixture `StellarNavigator.Tests/sn3_reference.json` (267 cases)
was generated by `tools/sn3_make_reference.py` (fixed seed 20261011) using
**only** `spkezr` — never raw records or the managed evaluator:

- J2000 × 8 states (5 SSB-relative + 3 Earth-relative pairs)
- SN2 30-day-grid epochs × 38 states
- **150 deterministic off-grid random epochs** (120 SSB-relative across all
  bodies + 30 Earth-relative pairs), uniform over full coverage
- First/last coverage epochs × 16 states
- 55 record-boundary cases (exact junctions ± {0, 1 ms, 0.5 s} on 4/16/32-day segments)

Comparison is component-wise (|Δ| per axis) in km and km/s.

| Metric | Value |
|---|---|
| Comparisons | 267 (150 off-grid; ≥100 required ✓) |
| Maximum position error | **1.192e-7 km (0.12 mm)** — worst case: Jupiter at a segment boundary |
| RMS position error | 3.449e-8 km |
| Maximum velocity error | **7.105e-15 km/s** |
| RMS velocity error | 2.066e-15 km/s |
| Boundary-case max errors | 1.192e-7 km / 3.553e-15 km/s (55 cases, zero failures) |
| Adopted test thresholds | 1e-6 km position, 1e-9 km/s velocity |
| Proposed engineering targets | 0.01 km / 1e-5 km/s — beaten by ~1e5× / ~1e9× |

Thresholds were set from observed maxima with margin (≈8× position,
≈1e5× velocity) for platform float differences — never relaxed to pass.
These numbers measure **managed-evaluator agreement with CSPICE**, not
absolute real-world navigational accuracy; SN3 makes no flight claim.

---

## 9. Supported time interval

Continuous evaluation is supported for TDB epochs JD **2396752.5 –
2506352.5** (≈1849-08-28 through 2150-08-01), identical for every segment.
Behavior outside is a deterministic `StaleData` failure; extrapolation is
never performed. Boundary epochs (first, last, exact junctions) succeed
deterministically.

---

## 10. Runtime performance (measured, Release, host reference machine)

| Metric | Value |
|---|---|
| Dataset file size | 61,163,371 bytes |
| Dataset load time | 1.85 s (single parse + validation + SHA-256) |
| Managed-heap after load | +26.5 MB |
| Process working set | ≈137 MB |
| Single-body evaluation | 1.4 µs (Jupiter) – 3.7 µs (Moon, 2-level chain) per call |
| Mixed 20,000-eval run | 2.4 µs/call sustained |
| Lookup strategy | O(1) index arithmetic per chain level; no record preloading beyond the parsed dataset |

No optimization targets were imposed before correctness was established.
Performance is predictable and bounded; per-record lazy loading is future
work if guideXOS-native memory budgets require it.

---

## 11. Remaining limitations

- Only 6 of 14 DE440s segments exported (SN2 body set + EMB helper);
  Mercury/Venus/Saturn/Uranus/Neptune/Pluto and Mercury-Venus centers
  report `InsufficientInformation`.
- Geometric states only; no light-time/stellar-aberration correction, no
  frame-transformation engine, no UTC/TAI/TT/TDB leap-second system.
- `ValidatedReference` means agreement with the stated CSPICE reference —
  not certification for flight navigation. No propulsion, guidance,
  hardware, autonomy, or sensor integration (explicitly excluded).
- UTC-epoch inputs use the approximate SN2 drift model; use TDB for full
  accuracy.
- Console UI only; no visualization changes.
- SN2 quirk preserved (not altered, to protect regression): the SN2
  provider's overall coverage endpoints derive from all rows including
  trivial SSB grid entries (JD 2396495–2506595), slightly wider than
  real-body sample coverage (2396765–2506325). The SN3 provider instead
  uses per-segment descriptor coverage (2396752.5–2506352.5).

---

## 12. Mars center versus barycenter investigation

**Outcome: the SN2 "~20 cm" offset claim is unverified and must not be
cited as source data.**

- `de440s.bsp` contains **no NAIF 499 (Mars center)** segment — only
  target 4 (Mars system barycenter) relative to the SSB. A direct
  `spkezr('499', …)` query fails with `SPICE(SPKINSUFFDATA)`.
- The ~20 cm figure therefore cannot be derived from the authenticated
  SN2/SN3 source kernel; it appears to be an order-of-magnitude
  recollection (plausibly Phobos/Deimos barycentric wobble scale), not a
  measured DE440s value.
- Correction applied: runtime and documentation label `mars` explicitly as
  **Mars system barycenter (NAIF 4)**; the extraction metadata records that
  499 is absent; no barycenter-for-center substitution is performed or
  implied. Verifying the true 4-vs-499 offset requires an additional kernel
  containing 499 (e.g. a Mars-satellite ephemeris or full DE440) and is
  recommended SN4-side work, not a blocker.

---

## 13. Offline and failure-mode testing

- Runtime has zero network/native/Python dependencies; the app runs fully
  offline (demonstrated).
- Missing dataset → explicit "SN3 dataset missing" message; SN2
  exact-sample output remains available and is distinctly labeled (never
  presented as continuous accuracy).
- Corrupt JSON, degree/record-length mismatch, altered metadata totals →
  `InvalidDataException` at load (tested).
- Checksum mismatch vs the embedded trusted digest → load refusal (tested
  with a tampered file).
- Unsupported bodies, out-of-coverage epochs (both sides), unsupported
  frames, NaN epochs, missing center records → distinct deterministic
  `NavigationResultStatus` failures (tested).
- Boundary epochs (first/last/junctions) succeed deterministically (tested,
  55 cases).

---

## 14. Build & test results

```
Build: SUCCESS (0 warnings, 0 errors) — Astronomy.Ephemeris, App, Tests
Tests: 92 passed, 0 failed (62 SN2 regression + 30 SN3)
  SN3: 11 evaluator unit tests, 16 provider behavior tests, 3 verification tests (267 fixture comparisons)
App:   Runs offline; SN2 section unchanged; SN3 off-grid query (body + JD args),
       Earth-relative distance, provenance/quality display, explicit out-of-coverage error.
```

The single remaining build warning (`xUnit2002` in SN2 `EphemerisTests.cs`)
predates SN3 and was left untouched.

---

## 15. Future compatibility opportunities (recommended SN4 scope)

1. Export remaining planet segments (1, 2, 6, 7, 8, 9) from the same kernel
   with the same validated pipeline.
2. Resolve a 499-capable kernel to settle the Mars center offset and
   support Mars-centered queries with explicit labeling.
3. Leap-second-table-based UTC↔TDB conversion from a bundled LSK-derived table.
4. Frame transformations (barycentric ↔ heliocentric ↔ planet-centered).
5. Memory-mapped / lazy record loading for guideXOS-native budgets.
6. Visualization only after numerical correctness (already locked here).

---

## 16. Safety

Research and simulation software — not qualified navigation equipment. All
JPL-derived results carry source/frame/origin/epoch metadata; simulated
spacecraft data is labeled as such. SN3 supports continuous evaluation
**within verified ephemeris coverage only** — not unlimited extrapolation
and not certified spacecraft navigation.
