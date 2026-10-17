# Stellar Navigator — Phase SN6

## Reference Frames & Spacecraft-Relative Navigation Coordinates

**Status:** Complete
**Date:** 2026-10-13
**Outcome:** A (Full Acceptance)
**Prior phase:** SN5 (Outcome A) — preserved in full (191/191 tests pass).
**Branch:** `STELLAR_NAVIGATOR` (starting HEAD `8d6179068b72a6623b3cac69496aa4a1069ecc2b`, `.phase` SN5)

---

## 1. Scientific model overview

SN6 extends the validated SN1–SN5 astronomical engine (JPL DE440s
ephemerides, continuous Chebyshev evaluation, indexed lazy loading,
authenticated UTC→ET timekeeping) with physically correct
reference-frame transformations and spacecraft-relative state arithmetic.

A state is located by three independent facts that SN6 never conflates:

- **Axis orientation** — *how the axes point* (`J2000` inertial or
  `IAU_EARTH` body-fixed). Changed only by a **frame rotation** (3×3) or a
  full **state transformation** (6×6, with velocity coupling).
- **Coordinate origin** — *where coordinates are measured from* (`ssb`,
  `earth`, …, `spacecraft:<id>`). Changed only by an **origin translation**
  (vector subtraction, axes untouched).
- **Epoch and time scale** — ET seconds past J2000 (TDB) for orientation
  evaluation; UTC labels pass exclusively through the SN5 conversion
  policies. UTC is never treated directly as ET.

Three operations exist, with deliberately distinct names so callers cannot
confuse them (`Astronomy.Core.StateTransformMath`,
`Navigation.Core.OriginTranslation`):

| Operation | API | Mathematics |
|---|---|---|
| Position-only frame rotation | `RotatePosition(R, r)` | r_dest = R · r_src (NO velocity implied) |
| Full state transformation | `TransformState(R, R′, r, v)` | r_dest = R·r_src; v_dest = R·v_src + R′·r_src |
| Proper inverse state transform | `InverseTransformState(R, R′, …)` | r_src = Rᵀ·r_dest; v_src = Rᵀ·v_dest − Rᵀ·R′·Rᵀ·r_dest |
| Origin translation | `ExpressRelativeTo(target, origin, newOrigin)` | Δr = r_t − r_o; Δv = v_t − v_o, same axes/epoch |

The 6×6 state transform has the block structure `[[R, 0], [R′, R]]`.
The inverse is **never** computed by transposing the 6×6 matrix (that moves
R′ to the wrong block); the velocity-coupling inverse above is implemented
and tested directly. All quantities are double precision, SI units
(m, m/s), finite-validated.

---

## 2. Frames supported

Explicit, limited registry (`Astronomy.Core.FrameRegistry`):

- **`J2000`** — inertial Earth-mean-equator axes (NAIF frame 1). Baseline
  preserved from SN1–SN5. Aliases accepted: `J2000`, `ICRF_J2000`.
- **`IAU_EARTH`** — Earth body-fixed axes from the authenticated IAU 2015
  polynomial model (NAIF frame 130399). Low-precision cartographic model.
  The framed service requires a verified immutable model instance; J2000
  queries remain available without the orientation asset.
- **Spacecraft-relative translated frame** — J2000-aligned axes with a
  spacecraft origin (`spacecraft:<id>`). This is a *translation*, not a new
  orientation: it does not rotate because a camera moves, and it is NOT a
  vehicle body frame (no attitude).

Arbitrary frame names are rejected with a diagnostic; there is no silent
fallback to J2000. In particular, `ITRF93` / `EARTH_FIXED` / `ITRF` are
refused with an explicit note that `IAU_EARTH` must not be substituted for
high-precision terrestrial frames (see §4).

Spacecraft attitude is a minimal future boundary only
(`Navigation.Core.ISpacecraftAttitudeProvider`,
`SpacecraftAttitudeState` with orientation/angular-velocity/epoch/source/
validity). No attitude is determined or fabricated in SN6; the single
synthetic helper (`SimulatedBodyFrame.Identity`) is labeled
`SIMULATION-ONLY` and is excluded from the validated path.

---

## 3. Source and destination conventions

- Rotation APIs take the **source→destination** pair implicitly through the
  (R, R′) evaluated for that direction at epoch ET:
  a verified `IauEarthOrientation` instance's `GetRotation(et)` returns the
  **J2000→IAU_EARTH** rotation and its analytical derivative.
- `TransformState` maps J2000 states to IAU_EARTH states;
  `InverseTransformState` maps back. Round trips
  (J2000→IAU_EARTH→J2000) recover inputs to ~1e-12 relative (452/452
  fixture cases).
- `FramedStateService` (ephemeris layer) composes **origin translation
  first, then orientation/velocity transformation**, and records both steps
  in provenance.

---

## 4. IAU_EARTH vs high-precision terrestrial frames

`IAU_EARTH` implemented here is the **low-precision IAU cartographic
rotation model**: a quadratic-polynomial pole plus uniformly rotating prime
meridian. It contains **no** nutation corrections, polar motion, UT1–TAI,
or EOP data. It is therefore **NOT equivalent** to:

- ITRF93 or any ITRF realization,
- the high-accuracy binary Earth PCKs (`earth_*.bpc`, EOP-based),
- VLBI/SLR-determined Earth orientation products.

Rotation-phase disagreement with true Earth-fixed axes can reach
arcseconds and grows outside a few centuries from J2000. Requests for
`ITRF93` are refused rather than served with `IAU_EARTH`. No SN6 result
may be presented as a terrestrial-survey or flight-navigation frame.

---

## 5. Coordinate origins and translation rules

- Supported origins in framed queries: `ssb` and any provider-supported
  body id (`earth`, `mars`, …). Spacecraft origins (`spacecraft:<id>`)
  require the explicit spacecraft-relative API.
- Translation requires identical orientation, identical epoch
  (|ΔET| ≤ 1e-6 s), identical SI units, and finite vectors; violations are
  typed `InvalidInput` failures.
- Both operands must also use the same original coordinate origin. The
  requested result origin is applied only after subtraction and does not
  alter that input requirement.
- Quality rule: translation of two `ValidatedReference` states stays
  `ValidatedReference`; stale quality remains stale, and approximate or
  simulated inputs remain approximate/simulation-derived. Failure statuses
  cannot be used as successful state inputs. `IsSimulated` travels separately
  so it remains set even when stale quality has precedence.

---

## 6. 3×3 rotation mathematics

Per NAIF `pck.req`, with T = ET centuries past J2000 (TDB) and
d = ET days past J2000 (TDB):

- RA(T) = RA₀ + RA₁·T + RA₂·T², DEC(T) likewise (deg → rad),
- W(d) = W₀ + W₁·d + W₂·d² (deg → rad),
- R(J2000→body-fixed) = [W]₃ [π/2−Dec]₁ [π/2+RA]₃ (passive-Euler product),
  i.e. R = Rz(−W)·Rx(−(π/2−Dec))·Rz(−(π/2+RA)) in active-matrix terms.

Authenticated constants (body 399, `pck00011.tpc`):

- POLE_RA = (0, −0.641, 0), POLE_DEC = (90, −0.557, 0)
  (deg, deg/century, deg/century²),
- PM = (190.147, 360.9856235, 0) (deg, deg/day, deg/day²),
- Radii (6378.1366, 6378.1366, 6356.7519) km (reference only).

---

## 7. 6×6 state-transform mathematics

v_dest = R·v_src + R′·r_src, with **analytical** R′ = dR/dt from the chain
rule over RA/Dec/W rates (no finite differences on the validated path).
Consequences verified by test:

- An inertially stationary off-axis point (e.g. Earth-surface scale,
  v = 0) has nonzero IAU_EARTH velocity ≈ 465.1 m/s — proving the law is
  not rotation-only. A rotation-only "velocity transform" (R·v) gives 0
  there and is wrong by the full coupling term.
- Distant-object rotating-frame velocities are large and correct
  (e.g. Mars-relative-Earth IAU_EARTH velocity ≈ 20 000 km/s from
  R′·r coupling at ~2.8e11 m — Earth-rotation rate times distance).
- Composition law R = R₂R₁, R′ = R₂′R₁ + R₂R₁′ matches sequential
  application; orthogonality error < 5e-15; det = +1 to 1e-12.

---

## 8. PCK source and checksum

| Item | Value |
|---|---|
| Filename | `pck00011.tpc` |
| Source URL | https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc |
| Retrieval date | 2026-10-13 |
| SHA-256 | `3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1` |
| Reference | Archinal et al., IAU WGCCRE 2015; NAIF release 2022-12-27 |
| Constants used | BODY399_POLE_RA/DEC/PM as §6 (polynomial only, no nutation terms) |
| Supporting kernels | None at runtime; host verification used only this PCK |
| Frame ids | J2000 (NAIF 1) → IAU_EARTH (NAIF 130399) |
| Runtime representation | Narrowly scoped managed model + 1437-byte compact asset `earth_orientation.sn6json` (schema SN-FRAME-01, SHA-256-verified at load). No general-purpose PCK parser. |

Model validity: the polynomial evaluates for any finite ET; it is
**validated** over the shipped-ephemeris coverage (§9) and degrades as a
physical Earth model outside a few centuries from J2000. Precision
limitation: low-precision cartographic (§4); numerical representation
limits are §10.

---

## 9. Frame time coverage

- Orientation evaluation accepts any finite ET (TDB); CSPICE comparison
  coverage: ET ∈ [−4 756 320 000, +4 756 320 000] s (JD 2396495–2506595,
  the shipped-dataset span), including J2000, 3 historical, 3 modern,
  4 rotation-phase, 2 boundary, and 100 deterministic off-grid epochs.
- Framed ephemeris queries are additionally bounded by the SN4 provider
  coverage (JD 2396752.5–2506352.5 TDB); outside epochs return `StaleData`,
  never extrapolated values.
- UTC queries inherit SN5 coverage policy (strict 1972-01-01…2017-01-01,
  provisional opt-in, pre-1972 refusal).

---

## 10. Independent CSPICE validation methodology

Host tooling only: `tools/sn6_make_reference.py` (Python 3.12, spiceypy
8.2.0 = CSPICE N0067) loads **only** the authenticated `pck00011.tpc`,
asserts its SHA-256 and toolkit version, then records `pxform` rotations
and `sxform` 6×6 matrices plus `sxform`-applied expected outputs for
4 representative vectors (Earth-relative ~1 AU, spacecraft-relative
~1e9 m, surface-scale stationary point, zero edge case) at all 113 epochs
→ `StellarNavigator.Tests/sn6_reference.json`: **452 state-transform
comparisons (400 off-grid)**, generated exclusively from CSPICE, never
from the managed code.

Tolerances (representation-aware, documented in-test, not weakened):

| Quantity | Tolerance | Basis |
|---|---|---|
| R entries | abs 2e-10 | binary64 W-phase quantization (‖W‖ ~ 2e7° at boundaries) |
| R′ entries | abs 3e-14 | Ẇ × R-error scaling |
| Position | rel 1e-10, floor 1e-6 m | same W-phase analysis |
| Velocity | 1e-9·‖v‖ + 3e-14·‖r‖ + 1e-9 m/s | R-error and R′-error propagation |

Any formula-level defect (Euler-order swap, transposed product,
missing R′ coupling, degree/radian slip) exceeds these by orders of
magnitude — as demonstrated during development, when hand-expansion sign
errors produced discrepancies up to 2.0 and were caught by these tests
before correction.

Maximum and RMS discrepancies (managed vs CSPICE, 452 comparisons):

| Quantity | Max | RMS | Worst case |
|---|---|---|---|
| R entries | 3.076e-11 | 4.452e-12 | et = −4.756e9 s (boundary) |
| R′ entries | 2.243e-15 /s | 3.246e-16 /s | et = −4.756e9 s (boundary) |
| Position | 5.857 m (rel 3.872e-11) | 0.715 m | et = 3.873e9 s, Earth-relative vector |
| Velocity | 4.269e-4 m/s | 5.208e-5 m/s | et = 3.873e9 s, Earth-relative vector |
| Inverse vs CSPICE (max) | rel 3.872e-11 / 1.214e-6 m/s | — | — |
| Round trips J2000→IAU→J2000 | 452/452 recover inputs | — | — |

Near J2000, agreement is ~1e-16 (R) — the boundary residuals are pure
binary64 phase quantization, identical in kind on both implementations.
Matching CSPICE is an implementation-validation result, not flight
certification.

---

## 11. Spacecraft-relative state model

For spacecraft and target sharing J2000 orientation, origin, and epoch:
relativePosition = target − spacecraft,
relativeVelocity = target-velocity − spacecraft-velocity, labeled in the
J2000-aligned translated frame `spacecraft:<id>`. Required handling
(all tested): missing spacecraft state (typed `InvalidInput`, never an
exception leak from the service), epoch/origin/frame mismatches,
unavailable ephemeris (propagated status), invalid simulated states,
non-finite vectors, unsupported conversions. Spacecraft position is taken
only from `SpacecraftState` — never inferred from camera coordinates
(regression test with a decoy camera position), and never assumed measured.

---

## 12. Simulated versus observed state distinction

`CartesianState.IsSimulated` plus `Quality` travel with every derived
state. A simulated spacecraft contribution keeps all downstream results
simulation-derived (`ApproximateOrSimulated`); validated frame mathematics
never promotes them to `ValidatedReference`. The console demo labels its
spacecraft `SIMULATED (not measured, not inferred from camera)`, and the
synthetic attitude helper carries a `SIMULATION-ONLY` marker excluded from
the validated path.

---

## 13. Failure and uncertainty handling

Typed, never silent: unsupported frames (`UnsupportedCalculation` +
diagnostic, no J2000 fallback); mismatched epochs/origins/orientations and
non-finite inputs (`InvalidInput`); unknown bodies
(`InsufficientInformation`); out-of-coverage epochs (`StaleData`, no
extrapolation); corrupt/missing orientation asset (`InvalidDataException`
at load — tamper test flips one byte and expects rejection); provisional
UTC epochs downgrade otherwise-validated states to `StaleData`.
JD-TDB labels carry the documented binary64 limit (~50 µs epoch
quantization, ~1–2 m at orbital speeds, ~0.5 m rotation-phase at 1 AU);
ephemeris and rotation always share the identical label so they stay
mutually consistent.

---

## 14. Offline runtime architecture

- Runtime projects (`Astronomy.Core`, `Navigation.Core`,
  `Astronomy.Ephemeris`) use only the .NET base class library; the SN6
  model is closed-form polynomials plus a 1437-byte JSON asset verified by
  embedded SHA-256. No CSPICE, Python, network, or OS orientation data at
  runtime. CSPICE/spiceypy exist solely as host-side generation tooling.
- The SN4 Chebyshev evaluator, indexed loader, and SN5 time subsystem are
  untouched (only additive overloads/calls around them); the 4 MiB
  coefficient-cache default is unchanged and no unbounded caching was added
  (orientation evaluation is stateless; no cache at all).
- Console demo (`StellarNavigator.App`, SN6 section): Earth rel SSB/J2000;
  Mars rel Earth/J2000; Mars rel Earth in IAU_EARTH axes (position and
  velocity); Jupiter rel labeled-simulated spacecraft with frame, origin,
  ET, provenance; `ITRF93` diagnostic. Existing SN2–SN5 output preserved
  verbatim.

---

## 15. Performance measurements

Desktop host-only measurements (Release, .NET 8, no bare-metal or guest
qualification claimed):

| Item | Value |
|---|---|
| Frame-data asset size | 1437 bytes |
| Asset load time (first, incl. SHA-256 + JSON) | 24.7 ms |
| One-time load heap transient | ~22 KB (desktop CLR) |
| Steady-state retained (orientation model) | none (static constants, stateless, no cache) |
| J2000→IAU_EARTH rotation + analytical derivative | ~135 ns/op |
| 3×3 position rotation | ~66 ns/op |
| 6-component state transform | ~246 ns/op |
| Inverse state transform | ~306 ns/op |
| Spacecraft-relative computation (one result object) | ~557 ns/op |
| SN4 cache budget | unchanged 4 MiB default |
| Incremental app startup cost | one 1.4 KB asset copy + ~25 ms one-time verification |

Repeated matrix operations allocate nothing on the heap
(`RotationMatrix` is a readonly struct); only result/state objects are
allocated at API boundaries.

---

## 16. Remaining limitations

- IAU_EARTH is low-precision cartographic (§4); no ITRF93/EOP support.
- No spacecraft attitude determination, sensors, hardware interfaces,
  autopilot, or propulsion (boundary interface only).
- No light-time/stellar-aberration corrections (geometric states, as SN4).
- No Mars-499/planet-center, Pluto, or new kernels; no trajectory
  optimization; no 2D/3D navigation UI (SN7 scope); no guideXOS SQL;
  no flight-certification claims.
- Direct UTC navigation queries now preserve ET through
  `IEtEphemerisProvider`; callers using the legacy AstroTime/JD interface
  retain its ~50 µs epoch quantization limit (see SN6R).

---

## 17. Recommended SN7 scope

Interactive Solar System navigation map built on the SN6 mathematics:
J2000 scene graph with on-demand IAU_EARTH overlays, spacecraft-relative
camera framing (keeping spacecraft state, camera, and translated frame
explicitly distinct), ET/UTC epoch scrubber reusing SN5 policies, and
direct-ET ephemeris queries. The direct-ET provider path is available from
SN6R; the map should surface the representation precision of legacy JD input
where that compatibility path is used.
No piloting/autopilot/propulsion control in SN7.
