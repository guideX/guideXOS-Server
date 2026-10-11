# Stellar Navigator — Phase SN2

## Real Ephemerides & Solar System Positioning

**Status:** Complete
**Date:** 2026-10-10
**Outcome:** A (Full Acceptance)

---

## 1. Ephemeris Strategy

**Selected: Approach C — generated offline reference dataset from recognized JPL ephemerides.**

### Rationale

| Approach | Evaluation |
|----------|------------|
| A. Native CSPICE interop | Rejected for SN2. guideXOS targets bare-metal/UEFI deployment; a native C dependency (CSPICE) cannot be assumed loadable in the guideXOS runtime, and P/Invoke marshalling of SPK evaluation adds validation surface without offline benefit once data is extracted. |
| B. Managed SPK reader | Rejected for SN2. A correct Chebyshev/SPK evaluator is a large undertaking; building one to avoid existing tools violates the phase constraint against custom SPICE reimplementation. |
| C. Generated offline dataset | **Selected.** Host-side scientific tooling (Python + CSPICE via spiceypy) extracts authentic state vectors from the official JPL kernel once; the runtime ships a compact JSON dataset and evaluates it with a small, fully tested managed provider. All SN2 functionality works offline with zero native dependencies. |

Host-only dependencies (Python, spiceypy, network for one-time kernel download) are documented below and are **not** runtime dependencies.

---

## 2. Scientific Sources

| Item | Value |
|------|-------|
| Kernel | `de440s.bsp` — JPL DE440s small-body/planet ephemeris |
| Source URL | https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp |
| Retrieval date | 2026-10-10 |
| Published MD5 (`aa_checksums.txt`) | `3917ee56769db332790c751e2168843d` |
| Verified local MD5 | `3917ee56769db332790c751e2168843d` — **match** |
| SPK SHA-256 | `c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2` |
| Leapseconds kernel (host extraction only) | `naif0012.tls` |
| Extraction tooling (host only) | Python 3.12 + spiceypy 8.2.0 (CSPICE N0067) |

### Supported NAIF IDs

| Body | NAIF ID | Note |
|------|---------|------|
| SSB | 0 | Origin; always zero |
| Mars (system barycenter) | 4 | **Not** Mars center 499; offset ~20 cm — documented, not interchanged |
| Jupiter (system barycenter) | 5 | System barycenter |
| Sun | 10 | Relative to SSB |
| Moon | 301 | Relative to SSB (same origin as all other bodies) |
| Earth (geocenter) | 399 | **Not** Earth-Moon barycenter 3 — documented, not interchanged |

Mercury, Venus, Saturn, Uranus, Neptune are present in `de440s.bsp` but were not extracted for SN2; the provider reports them as unsupported with a clear diagnostic.

---

## 3. Coordinate Conventions

- **Reference frame:** J2000 (ICRF-compatible inertial axes, per source kernel).
- **Origin:** Solar System Barycenter (NAIF ID 0) for **all** stored bodies, including the Moon.
- **Aberration correction:** NONE — geometric states only. No light-time or stellar-aberration correction is applied, and results are never labeled as apparent/observed positions.
- **Relative states:** `relativePosition = targetPosition − observerPosition`, `relativeVelocity = targetVelocity − observerVelocity`, computed only for matching epoch, frame, and origin. Separation distance is the magnitude of the relative position vector.

---

## 4. Time-Scale Conventions

- **Ephemeris time:** TDB (Barycentric Dynamical Time). Sample epochs are Julian Dates in TDB (`JDTDB` via CSPICE `UNITIM`).
- **UTC input:** accepted by the time API but **never silently equated** with TDB. `TdbTime.UtcToTdb` applies the documented linear drift model; conversions that cannot be verified are refused by returning explicit failures.
- **Simulation clock:** independent of the OS wall clock. Validation uses exact precomputed TDB epochs (notably J2000, JD 2451545.0 TDB).
- **Grid:** 30-day samples anchored exactly at J2000 (JD 2451545.0 TDB), covering JD 2396495–2506595 (≈1849-03 through 2150-10).

---

## 5. Units

- **Dataset file:** kilometers (position), kilometers/second (velocity).
- **Runtime internals:** SI — meters, meters/second (converted on load via `Distance.MetersPerKilometer`).
- Rendering coordinates are never used in calculations; no renderer changes were made in SN2.

---

## 6. Dataset Generation Procedure

1. Download `de440s.bsp`; verify MD5 against `aa_checksums.txt`.
2. Load with spiceypy (`furnsh`) plus `naif0012.tls`.
3. Anchor grid at J2000: `epochs = ET(JD 2451545.0 TDB) + k·30 days`, k ∈ [−1835, 1835].
4. For each epoch and body, evaluate `spkezr(id, et, "J2000", "NONE", "0")` — geometric states, SSB observer.
5. Round to 6 decimals (position, km) / 9 decimals (velocity, km/s); embed provenance metadata; write `ephemeris_de440s.json` (21,936 entries, 6 bodies × 3,671 epochs).
6. Independently re-evaluate a reference fixture (`sn2_reference.json`) with the same tooling at grid-aligned epochs; tests compare provider output against this fixture.

The extraction scripts are host tooling and are not shipped with the runtime.

---

## 7. Reference-Validation Results

Independent golden-fixture comparison at J2000 (JD 2451545.0 TDB):

| Check | Provider | Independent reference | Difference |
|-------|----------|----------------------|------------|
| Earth position magnitude | 146,888,944.945 km | 146,888,944.9 km | 0.045 km |
| Moon position magnitude | 146,673,978.863 km | 146,673,978.9 km | 0.037 km |
| Mars position magnitude | 207,058,088.975 km | 207,058,089.0 km | 0.025 km |
| Jupiter position magnitude | 741,701,935.982 km | 741,701,936.0 km | 0.018 km |
| Earth velocity magnitude | 30.2853 km/s | reference | < 0.001 km/s |
| Earth–Moon distance | 402,448.6 km | 402,448.6 km | 0.0 km |
| Earth–Mars distance | 276,696,809.0 km | 276,696,809.0 km | < 10 km |
| Earth–Jupiter distance | 691,310,717.4 km | 691,310,717.4 km | < 10 km |

Tolerances (1 km position, 0.001 km/s velocity, 1–10 km distances) reflect dataset rounding (mm-level position, nm/s-level velocity), **not** kernel accuracy. No continuous-accuracy claim is made: only exact sample epochs are served; anything else returns an explicit unavailable result. No extrapolation is performed.

---

## 8. Offline Execution

- Dataset ships at `Astronomy.Ephemeris/Data/ephemeris_de440s.json`, copied to the app output directory at build time.
- No network access at startup or runtime; no NASA/cloud dependency.
- Missing file → `FileNotFoundException` with path; corrupt JSON → `InvalidDataException`.
- Structural validation on load: non-empty entries, entry-count vs metadata total, per-entry NAIF-ID cross-check.
- SHA-256 of the loaded file is exposed (`JplEphemerisProvider.Sha256`) for provenance display.
- Unsupported bodies, out-of-coverage epochs, non-sample epochs, and unsupported frames each return distinct `NavigationResultStatus` failures with messages.
- Repeated evaluation is deterministic (binary search over sorted samples).

---

## 9. Runtime Dependencies

None beyond the existing .NET 8 runtime and SN1 project references. Python/spiceypy/network were used **once, on the host**, to generate the dataset.

---

## 10. Known Unsupported Cases

- Bodies not extracted (Mercury, Venus, Saturn, Uranus, Neptune, Pluto) → `InsufficientInformation`.
- Epochs outside JD 2396495–2506595 → `StaleData`.
- Non-sample epochs (no interpolation in SN2) → `StaleData` with sample-interval guidance.
- Frames other than Barycentric/ICRF-J2000 → `UnsupportedCalculation`.
- UTC↔TDB conversion uses a linear drift model; leap-second-precise conversion is **not** claimed.
- Spacecraft state remains explicitly **simulated**; no measurement, propulsion, guidance, or hardware integration.
- Console UI only; no graphical visualization changes.

---

## 11. Precision Limitations

- Sample quantization: 30-day grid; state queries are exact-sample only.
- Rounding: 1 mm position, 1 nm/s velocity in the file.
- Mars = system barycenter (499 offset ~20 cm unmodeled); Earth = geocenter (EMB offset unmodeled in stored vectors — relative Earth–Moon geometry is computed from consistent SSB-relative vectors).
- Geometric states only; light-time correction and relativistic effects absent by design.

---

## 12. Recommended Path Toward Full SPICE Support (SN3+)

1. Ship a bounded binary SPK subset (or retain the JSON dataset) and add a managed Chebyshev evaluator **validated against CSPICE** sample-by-sample before replacing exact-sample lookup.
2. Add supported-body coverage for the remaining planets from the same kernel.
3. Add frame transformations (barycentric ↔ heliocentric ↔ planet-centered) with tests.
4. Add leap-second-table-based UTC↔TDB conversion using a bundled LSK-derived table.
5. Consider 2D visualization only after numerical correctness is locked; keep rendering coordinates isolated.

---

## 13. Build & Test Results

```
Build: SUCCESS (0 warnings, 0 errors)
Tests: 62 passed, 0 failed (44 SN1 regression + 18 SN2)
App:   Runs offline; JPL-derived positions with ValidatedReference status;
       unsupported bodies/epochs report clear diagnostics.
```

---

## 14. Safety

Research and simulation software — not qualified navigation equipment. No piloting, propulsion, guidance, sensor, or certification claims. All JPL-derived results carry source/frame/origin/epoch metadata; simulated spacecraft data is labeled as such.
