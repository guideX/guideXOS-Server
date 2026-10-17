# Stellar Navigator — Phase SN6R

## Independent Code Review & Scientific Contract Hardening

**Status:** Complete after validation gates below
**Branch:** `STELLAR_NAVIGATOR`
**Scope:** Focused SN5/SN6 correctness and public API contract repairs
**Qualification:** Research and simulation software; not flight-qualified.

## 1. Starting baseline

| Item | Starting value |
|---|---|
| Required branch | `STELLAR_NAVIGATOR` |
| Actual starting HEAD | `832dbc231296cf19188a60b2b5ae04f0bbc1a25b` |
| Prompt's expected HEAD | `b25096ff39bff7cc9cb71c3450625480a7593196` |
| Relationship | Actual HEAD is a direct child of the expected commit |
| `.phase` | `SN6` |
| Initial worktree | Clean; no tracked or untracked modifications |
| Active checkout agents | None |
| SN6R state | Not complete; no SN6R acceptance report or phase marker existed |
| Baseline regression | 244 passed, 0 failed, 0 skipped |

The newer starting commit adds tracked build outputs under `bin/obj` and small
scaffold source files. They were preserved. The implementation review covered
the accepted SN5 and SN6 reports, the implicated source and callers, and the
SN1–SN6 fixtures. The pre-existing xUnit analyzer warning in
`EphemerisTests.cs` was present in the baseline run.

## 2. Finding assessment and repairs

| Finding | Assessment | Result |
|---|---|---|
| A — Origin subtraction | **Confirmed** | Require matching source origins and consistent TDB epoch metadata before subtraction. |
| B — Earth orientation verification | **Confirmed** | Validated rotating-frame service queries require a verified immutable model instance. |
| C — Quality propagation | **Confirmed** | Apply conservative quality precedence and reject unsuccessful input qualities. |
| D — JD conversion quality | **Confirmed** | Separate time-data trust from numeric representation precision. |
| E — UTC ET quantization | **Confirmed** | Add ET-native evaluation and keep the legacy JD API intact. |
| F — UTC constructor validation | **Confirmed** | Validate constructor fields and revalidate object inputs, including default structs. |

### A. Origin translation

The original `ExpressRelativeTo()` checked axes, ET difference, and matching
units but did not compare `target.OriginId` with `originBody.OriginId`. A
state based at the SSB could therefore be subtracted from one based at Earth
and receive a plausible relative-state result. The result origin parameter
does not make those source vectors comparable.

The method now requires identical source-origin metadata, matching supported
orientation, finite SI values, a TDB epoch label, and agreement between each
label and its ET value within 1 ms (to allow the existing JD label's binary64
rounding). It then retains the existing 1 µs pairwise epoch tolerance and
applies the requested new origin only to the result. Positive shared-origin
and negative mismatched-origin cases are covered.

### B. Authenticated `IAU_EARTH` source

Before SN6R, the application loaded and authenticated
`earth_orientation.sn6json`, but `FramedStateService` accepted only an
ephemeris provider and called the static rotation evaluator. A library caller
could request a validated Earth-fixed framed state without loading the asset.

`IauEarthOrientation.Load()` now returns an immutable authenticated instance.
The public `GetRotation()` and `EvaluateAngles()` evaluators are instance
methods and reject the unauthenticated compatibility `Instance`. The framed
service accepts the verified model as an explicit optional constructor
dependency: J2000 paths work without it, while `IAU_EARTH` requests fail with
`InsufficientInformation` until the trusted asset loads. Missing and corrupt
assets fail closed. The console application now passes the verified model to
the service.

The analytical rotation and derivative formulas were not changed. Existing
formula and CSPICE tests now load the authenticated model before evaluation.

### C. State quality composition

The prior fallback selected `target.Quality` whenever both operands were not
validated and no input was simulated. Thus `ValidatedReference` minus
`StaleData` returned `ValidatedReference`, while reversing the operands
returned stale quality.

The new documented precedence is:

1. Reject `InvalidInput`, `InsufficientInformation`, and
   `UnsupportedCalculation`; they cannot produce a state.
2. Preserve `StaleData` if either usable operand is stale.
3. Otherwise preserve `ApproximateOrSimulated` if either operand is
   approximate or simulation-derived.
4. Return `ValidatedReference` only when both operands are validated and
   non-simulated.

`IsSimulated` propagates independently, including when stale status takes
precedence. The full ordered quality matrix tests every status pairing in
both orders and both simulation-flag values. The independent validated plus
validated case remains validated.

### D. Time-data quality versus JD precision

`TryUtcToJdTdb()` previously rewrote `ProvisionalStaleTable` as
`PrecisionLimited`. It now preserves `UtcToEtResult.Quality` as the trust and
validity of the time conversion and reports the binary64 representation as
`RepresentationPrecision` (`EtSeconds` or `JulianDateDouble`). The old
`PrecisionLimited` enum member remains for source compatibility but is not
used as a combined quality by the new paths. Provisional UTC remains
provisional through JD conversion and still downgrades successful
downstream navigation results to `StaleData`.

### E. ET-preserving ephemeris evaluation

Both UTC query surfaces previously formed an `AstroTime` from `JdTdb`; the
providers reconstructed ET from that rounded JD before selecting and
evaluating the same Type 2 Chebyshev record. SN6 transforms also consumed
that JD-derived epoch. This adds tens of microseconds of representation
quantization even though SN5 already has precise ET seconds.

The additive `IEtEphemerisProvider.GetStateAtEt()` path is implemented by
both the indexed and full managed Chebyshev providers. `UtcEphemerisQuery`
and `FramedStateService.GetStateUtc()` use the exact converted ET when that
path is available. Record selection and coverage comparisons now consume
that exact ET directly. `FramedStateService.GetStateAtEt()` also evaluates
target and origin states at the same ET before applying the frame rotation.
Spacecraft-relative queries use the spacecraft ET metadata rather than
round-tripping its JD label. Existing AstroTime/JD entry points remain
available and are clearly identified as compatibility paths.

No Chebyshev coefficients, Type 2 evaluator mathematics, velocity
derivative, center-chain composition, or default 4 MiB cache budget changed.
This reduces representation error; it does **not** increase the physical
accuracy of DE440s.

Deterministic UTC precision case:

| Input | Direct ET | ET reconstructed from JD | Epoch difference | Earth position change |
|---|---:|---:|---:|---:|
| `2016-07-14T00:00:00.123456789Z` | `521726468.3071998 s` | `521726468.3072135 s` | `13.709 µs` | `0.401606 m` |

The regression test asserts at least 10 µs of epoch difference and over 0.1 m
of evaluated position difference for that timestamp. In an independent
`sn4_reference.json` case, native ET evaluation and the legacy JD query both
remain within the established CSPICE thresholds: max position error
`5.960e-8 km`, max velocity error `8.882e-16 km/s` for that case. No existing
CSPICE tolerance was weakened.

### F. UTC object construction and conversion

The public `UtcInstant` constructor previously stored arbitrary fields. It
now rejects invalid years, month/day combinations, out-of-range hours,
minutes and seconds, illegal `:60` placement, and negative, non-finite, or
out-of-range fractions with `ArgumentOutOfRangeException` naming the invalid
field. The parser still returns its existing typed parse failure.

`TimeConverter.TryUtcToEt(UtcInstant)` independently validates the value so
`default(UtcInstant)` cannot bypass validation. Syntactically valid
`23:59:60` is still accepted only on a leap day in the authenticated table;
an unannounced leap date returns `InvalidUtcInput`. Tests cover real leap
second success, unsupported leap dates, and default-structure failure.

## 3. Legacy time conversion audit

The two production callers of the old UTC/TDB approximation were the
`ToEphemerisSeconds(AstroTime)` methods on the full and indexed Chebyshev
providers. Both paths now require `TimeScale.Tdb` and return typed
`InvalidInput` for UTC labels. Direct calls to `TdbTime.UtcToTdb()` and
`TdbTime.TdbToUtc()` no longer perform the pre-SN5 linear-drift conversion;
they throw `NotSupportedException` for actual cross-scale conversion. The
TDB identity compatibility path remains. There are no remaining production
references from the ephemeris providers to these legacy conversions.

## 4. API compatibility impact

- `IEtEphemerisProvider` and `GetStateAtEt()` are additive; existing
  `IEphemerisProvider.GetState(AstroTime, ...)` remains available.
- `FramedStateService` retains its one-argument constructor behavior for
  J2000 queries. Earth-fixed queries now require the optional authenticated
  model.
- `IauEarthOrientation.GetRotation()` and `EvaluateAngles()` changed from
  static calls to authenticated instance calls. This is an intentional API
  correction: an unauthenticated static evaluator was the bypass. Callers
  must load the asset and use the returned instance.
- `TryUtcToJdTdb()` preserves `Quality` as data trust and sets the new
  `RepresentationPrecision` property. Existing `PrecisionLimited` remains
  in the enum but is no longer substituted for provisional trust.
- `TdbTime` cross-scale UTC conversions now fail explicitly. TDB-only use
  remains supported.
- Invalid `UtcInstant` constructor inputs now throw immediately; the public
  object conversion overload reports invalid default structs as
  `InvalidUtcInput`.

## 5. Regression and scientific verification

All 244 SN1–SN6 baseline tests remain present and passing. SN6R adds 30
offline test cases covering:

- Source-origin, epoch/time-scale, unit and orientation metadata.
- Every ordered state-quality combination and simulated flag combination.
- Missing, corrupt, unverified and authenticated orientation data.
- UTC constructor fields, leap-second table validation, default structs and
  fractions.
- Provisional time through JD conversion and downstream state quality.
- Native ET versus legacy JD evaluation and both Chebyshev providers.
- Legacy UTC conversion rejection and TDB compatibility.

Validation results:

| Check | Result |
|---|---|
| Baseline regression before edits | 244/244 passed |
| Full Debug suite | 274/274 passed |
| Full Release suite | 274/274 passed |
| Full Debug solution build | Passed, 0 warnings, 0 errors |
| Full Release solution build | Passed, 0 warnings, 0 errors |
| Offline app smoke test | Passed through SN2–SN6, including UTC/leap-second and authenticated Earth-fixed queries |
| SN4 CSPICE fixture | 523 cases, 400 off-grid; max position `9.537e-7 km`, max velocity `1.421e-14 km/s` |
| SN4 CSPICE boundary fixture | 81 cases; max position `4.768e-7 km`, max velocity `7.105e-15 km/s` |
| SN4 Earth-relative fixture | 65 cases; max position `9.537e-7 km`, max velocity `1.421e-14 km/s` |
| SN6 CSPICE frame fixture | 452 transforms, 400 off-grid; unchanged tolerances and results below |
| SN5 CSPICE UTC→ET fixture | 206 cases; max error `1.192e-7 s` (0.1192 µs) |
| SN5 CSPICE leap-boundary fixture | 91 cases; exact at reported precision |
| SN5 CSPICE ET→UTC fixture | 40 exact ISO-C strings |

SN6 frame numerical comparison remained unchanged:

| Quantity | Max discrepancy | RMS |
|---|---:|---:|
| Rotation matrix entries | `3.076e-11` | `4.452e-12` |
| Rotation derivative entries | `2.243e-15 /s` | `3.246e-16 /s` |
| Transformed positions | `5.857 m` (`3.872e-11` relative) | `0.715 m` |
| Transformed velocities | `4.269e-4 m/s` | `5.208e-5 m/s` |

Round trips passed for all 452 cases. Existing app/test build output reports
one pre-existing xUnit analyzer warning at `EphemerisTests.cs:51`; it was
already present in the 244-test baseline. Full solution builds report zero
warnings and zero errors. No new compiler warnings were introduced.

## 6. Remaining limitations and SN7 recommendations

- The leap-second asset contains announced data through 2017. Later UTC
  conversion still requires explicit provisional opt-in and remains stale.
- `IAU_EARTH` remains the low-precision cartographic model. It is not
  ITRF93, EOP-based Earth orientation, or a flight navigation frame.
- The legacy AstroTime/JD API retains its binary64 JD epoch quantization;
  precision-sensitive callers should use `IEtEphemerisProvider` and the
  ET-native framed query.
- The SN6R precision correction addresses representation only; it is not a
  claim of greater DE440s physical accuracy or flight qualification.

SN7 should consume the ET-native path for UTC-driven map updates, load and
pass the verified orientation model explicitly, display stale/approximate/
simulated state quality separately from numeric precision, and preserve
source-origin, output-origin, and axis orientation as separate state labels.
No flight control or high-precision terrestrial frame behavior is implied.
