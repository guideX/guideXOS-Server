# Stellar Navigator — Phase SN5

## Astronomical Timekeeping & Leap-Second Validation

**Status:** Complete
**Date:** 2026-10-12
**Outcome:** A (Full Acceptance)
**Prior phase:** SN4 (Outcome A) — preserved in full.
**Branch:** `STELLAR_NAVIGATOR` (starting HEAD `1c8fcc66`, `.phase` SN4)

---

## 1. What SN5 changed

SN4 proved byte-exact, bounded-memory, eight-planet DE440s evaluation with
135/135 tests passing — but every epoch entered the engine as an ambiguous
`double` (a JD that might be UTC or TDB), converted from calendar dates with
`DateTime.Parse` (which cannot represent a leap second) and a linear-drift
stand-in for TDB. SN5 keeps that scientific engine byte-for-byte intact and adds:

1. **Explicit time scales** — UTC, TAI, TT, TDB, ET (seconds past J2000 TDB),
   JD TDB — as distinct representations. One untyped `double` is never again
   used to ambiguously mean UTC seconds, Julian dates, and TDB seconds.
2. **A strict UTC type** (`UtcInstant`) that CAN represent `23:59:60Z`,
   parses only `YYYY-MM-DDTHH:MM:SS[.fraction]Z`, and is locale-independent.
3. **An authenticated LSK-derived time-data asset** (`leap_seconds.sn5json`,
   1768 bytes, SHA-256 verified at load) carrying the 28 TAI–UTC transitions
   and DELTET constants of the official `naif0012.tls`.
4. **The justified chain UTC → TAI → TT → TDB** (LSK Moyer formulation, same
   as CSPICE DELTET) plus the inverse TDB → TT → TAI → UTC, with typed
   failures instead of silent assumptions.
5. **Explicit historical/future policy** — strict mode refuses pre-1972 and
   post-announcement epochs; an explicit provisional mode reproduces CSPICE
   behavior with a `ProvisionalStaleTable` quality that downgrades derived
   ephemeris results from `ValidatedReference` to `StaleData`.
6. **246 new independent CSPICE comparisons** (206 forward incl. 91
   leap-boundary, 40 exact inverse strings) with max error 0.12 µs.
7. **Console UTC queries** — Earth and Jupiter demonstrated at a supported
   UTC timestamp; direct JD TDB paths unchanged.
8. **56 new tests: 191/191 pass** (135 SN1–SN4 preserved) in Debug and Release.

---

## 2. Scientific time scales

| Scale | Meaning | Representation |
|---|---|---|
| UTC | Civil Coordinated Universal Time, incl. leap seconds | `UtcInstant` (calendar fields + fraction, second 60 allowed) |
| TAI | Atomic time, uniform | internal seconds staging (`uniform + DTA`) |
| TT | Terrestrial Time = TAI + 32.184 s | internal seconds staging |
| TDB | Barycentric Dynamical Time | `EtSeconds` (s past J2000), `JdTdb` double (compat) |
| ET | SPICE ephemeris seconds past J2000 TDB | `EtSeconds.Value` (identical to TDB-past-J2000) |

A physical instant, its representation in a time scale, and its calendar
formatting are distinct: `EtSeconds` is the stable internal representation
for precision-sensitive work; `UtcInstant` is a calendar label; the existing
`AstroTime` JD entry points are preserved for compatibility. Time-data trust
is reported by `UtcToEtResult.Quality`; JD-double quantization is reported
independently by `RepresentationPrecision`.

UTC, TAI, TT, and TDB are never treated as interchangeable. The old
`TdbTime.UtcToTdb` and `TdbToUtc` linear-drift approximations (≈1.7e-9
relative drift, no leap seconds) are disabled for UTC conversion; their
TDB-only identity compatibility remains. Use `TimeConverter` for UTC.

### The same fields labeled UTC and TDB are different instants

`2016-12-31T23:59:59` **UTC** and the same fields read on the **TDB** scale
differ by DELTA_ET ≈ 68.18 s (TAI–UTC 36 s + TT–TAI 32.184 s + ~−70 µs
periodic). Converting a UTC calendar date by merely reformatting its fields
as TDB misplaces every planet by up to ~2 km (Earth moves ~30 km/s).
The SN5 converter resolves this explicitly; the console prints both the
source UTC string and the resolved ET/JD TDB for every query.

---

## 3. NAIF LSK source and checksum

| Item | Value |
|---|---|
| Kernel | `naif0012.tls` — current latest in the NAIF LSK directory at retrieval |
| Source URL | https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/naif0012.tls |
| Directory listing date | 2016-07-14 (no `naif0013` published as of retrieval) |
| Retrieval date | 2026-10-12 |
| Original SHA-256 | `678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b` |
| File size | 5257 bytes |
| Last file modification note | "2016, Jul. 14 … leapsecond … on December 31, 2016" |
| Last announced leap second | 2016-12-31T23:59:60Z (effective 2017-01-01T00:00:00Z) |
| Host tooling | Python 3.12 + spiceypy 8.2.0 (CSPICE N0067); file re-downloaded and re-hashed at fixture generation |

No future leap-second announcements are invented. The LSK's last entry is
NOT treated as an expiration date: the kernel remains the authoritative
statement of announced transitions; dates after it use the last recorded
offset only under an explicit provisional opt-in (see §6).

SPICE reference: https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/time.html

---

## 4. Leap-second transition table (TAI–UTC, seconds)

Effective at the stated UTC midnight; the leap second itself sits on the
preceding evening (23:59:60). All 27 steps are exactly +1 (validated at
asset generation AND at runtime load).

| # | Effective UTC | TAI–UTC | Leap evening |
|---|---|---|---|
| 1 | 1972-01-01T00:00:00Z | 10 | — (history anchor; no 1971-12-31 leap) |
| 2 | 1972-07-01T00:00:00Z | 11 | 1972-06-30 |
| 3 | 1973-01-01T00:00:00Z | 12 | 1972-12-31 |
| 4 | 1974-01-01T00:00:00Z | 13 | 1973-12-31 |
| 5 | 1975-01-01T00:00:00Z | 14 | 1974-12-31 |
| 6 | 1976-01-01T00:00:00Z | 15 | 1975-12-31 |
| 7 | 1977-01-01T00:00:00Z | 16 | 1976-12-31 |
| 8 | 1978-01-01T00:00:00Z | 17 | 1977-12-31 |
| 9 | 1979-01-01T00:00:00Z | 18 | 1978-12-31 |
| 10 | 1980-01-01T00:00:00Z | 19 | 1979-12-31 |
| 11 | 1981-07-01T00:00:00Z | 20 | 1981-06-30 |
| 12 | 1982-07-01T00:00:00Z | 21 | 1982-06-30 |
| 13 | 1983-07-01T00:00:00Z | 22 | 1983-06-30 |
| 14 | 1985-07-01T00:00:00Z | 23 | 1985-06-30 |
| 15 | 1988-01-01T00:00:00Z | 24 | 1987-12-31 |
| 16 | 1990-01-01T00:00:00Z | 25 | 1989-12-31 |
| 17 | 1991-01-01T00:00:00Z | 26 | 1990-12-31 |
| 18 | 1992-07-01T00:00:00Z | 27 | 1992-06-30 |
| 19 | 1993-07-01T00:00:00Z | 28 | 1993-06-30 |
| 20 | 1994-07-01T00:00:00Z | 29 | 1994-06-30 |
| 21 | 1996-01-01T00:00:00Z | 30 | 1995-12-31 |
| 22 | 1997-07-01T00:00:00Z | 31 | 1997-06-30 |
| 23 | 1999-01-01T00:00:00Z | 32 | 1998-12-31 |
| 24 | 2006-01-01T00:00:00Z | 33 | 2005-12-31 |
| 25 | 2009-01-01T00:00:00Z | 34 | 2008-12-31 |
| 26 | 2012-07-01T00:00:00Z | 35 | 2012-06-30 |
| 27 | 2015-07-01T00:00:00Z | 36 | 2015-06-30 |
| 28 | 2017-01-01T00:00:00Z | 37 | 2016-12-31 |

### Leap-second convention (precise)

- Uniform UTC seconds count 86400 s/day. A `23:59:60.f` label maps to the
  same uniform count as the following midnight (`S + f`); the physical extra
  second enters through the TAI–UTC offset step — exactly the CSPICE DELTET
  convention (verified: consecutive +1.0000000 s ET steps across
  `:59 → :60 → :00`, and SPICE `deltet` at the leap ET equals
 
...[truncated 13787 chars]
