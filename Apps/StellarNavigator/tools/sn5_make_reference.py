#!/usr/bin/env python3
"""Stellar Navigator SN5 — independent CSPICE time-conversion reference fixture.

HOST TOOLING ONLY (Python + spiceypy/CSPICE N0067 loaded with the SAME
authenticated LSK the runtime asset was derived from). Expected values come
exclusively from the reference implementation (str2et / et2utc / deltet),
never from the managed converter under test.

Usage:
    python sn5_make_reference.py <naif00XX.tls> <output.json>

Coverage (deterministic, seed 20261012):
    1. J2000-era dates (incl. fractions, day boundaries).
    2. 1980s / 1990s dates.
    3. Every supported positive leap second (27 events): 23:59:59, the
       23:59:60 instant itself, and the following 00:00:00 midnight.
    4. 2016-12-31 transition in detail (fractions around the leap second).
    5. Contemporary UTC dates (provisional regime: past the last table entry;
       CSPICE reuses the last offset — the managed provisional mode must agree
       numerically while flagging quality explicitly).
    6. Fractional-second dates.
    7. Calendar year/month boundaries (incl. Feb 29 and non-leap rollovers).
    8. Invalid leap-second inputs (managed code MUST reject; SPICE values, if
       any, are recorded only to document that SPICE normalizes silently).
    9. Inverse conversions (ET -> UTC ISOC strings at millisecond grid).
    10. Time-policy edges (first strict instant, last strict instant,
        far-future provisional, pre-1972 historical refusals).
    11. Random UTC instants across the modern interval (seeded).

Leap-second event list: day before each DELTA_AT effective date after the
1972-01-01 anchor (27 events, offsets 11..37), matching the LSK table.
"""

from __future__ import annotations

import hashlib
import json
import sys
from datetime import datetime, timedelta, timezone

import numpy as np
import spiceypy as spice

SEED = 20261012
EXPECTED_KERNEL_SHA256 = "678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b"

# Effective UTC dates (from the LSK DELTA_AT table); leap event i>=1 sits on
# the evening before EFFECTIVE[i].
EFFECTIVE = [
    "1972-01-01", "1972-07-01", "1973-01-01", "1974-01-01", "1975-01-01",
    "1976-01-01", "1977-01-01", "1978-01-01", "1979-01-01", "1980-01-01",
    "1981-07-01", "1982-07-01", "1983-07-01", "1985-07-01", "1988-01-01",
    "1990-01-01", "1991-01-01", "1992-07-01", "1993-07-01", "1994-07-01",
    "1996-01-01", "1997-07-01", "1999-01-01", "2006-01-01", "2009-01-01",
    "2012-07-01", "2015-07-01", "2017-01-01",
]


def leap_days():
    out = []
    for eff in EFFECTIVE[1:]:
        d = datetime.strptime(eff, "%Y-%m-%d").replace(tzinfo=timezone.utc)
        out.append((d - timedelta(days=1)).strftime("%Y-%m-%d"))
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <naif00XX.tls> <output.json>")
        return 2
    tls, output = sys.argv[1], sys.argv[2]

    sha = hashlib.sha256(open(tls, "rb").read()).hexdigest()
    assert sha == EXPECTED_KERNEL_SHA256, f"LSK not authenticated: {sha}"
    spice.furnsh(tls)
    # Confirm the furnished kernel really is naif0012 content.
    assert abs(spice.str2et("2000-01-01T12:00:00") - 64.18392728473108) < 1e-6

    forward = []   # {utc, et, deltet, regime}
    invalid = []   # {utc, reason, spiceEtOrNull}
    inverse = []   # {et, utcIsoc3}

    def add_fwd(utc: str, tag: str):
        et = float(spice.str2et(utc))
        de = float(spice.deltet(et, "ET"))
        regime = "strict" if utc <= "2017-01-01T00:00:00" else "provisional"
        forward.append({"utc": utc, "tag": tag, "et": et,
                        "deltet": de, "regime": regime})

    def add_inv(utc: str):
        et = float(spice.str2et(utc))
        iso = spice.et2utc(et, "ISOC", 3)
        inverse.append({"et": et, "utcIsoc3": iso, "fromUtc": utc})

    # 1. J2000 era.
    for u in ["2000-01-01T12:00:00", "2000-01-01T12:00:00.5",
              "2000-01-01T11:59:59", "2000-01-01T00:00:00",
              "2000-01-02T00:00:00", "1999-12-31T23:59:59",
              "2000-02-29T12:30:45", "2000-02-29T23:59:59",
              "2001-02-28T23:59:59", "2001-03-01T00:00:00"]:
        add_fwd(u, "j2000")

    # 2. 1980s / 1990s.
    for u in ["1985-07-01T00:00:00", "1986-03-14T09:26:53",
              "1989-12-31T23:59:59", "1990-01-01T00:00:00",
              "1994-06-30T23:59:59", "1995-06-15T06:30:00",
              "1998-12-31T12:00:00", "1999-12-31T23:59:59"]:
        add_fwd(u, "historic")

    # 3. Every supported leap second: ...:59, ...:60, next 00:00:00.
    for day in leap_days():
        add_fwd(f"{day}T23:59:59", "leap")
        add_fwd(f"{day}T23:59:60", "leap")
        nxt = (datetime.strptime(day, "%Y-%m-%d").replace(tzinfo=timezone.utc)
               + timedelta(days=1)).strftime("%Y-%m-%d")
        add_fwd(f"{nxt}T00:00:00", "leap")

    # 4. 2016-12-31 transition in detail.
    for u in ["2016-12-31T23:59:58", "2016-12-31T23:59:58.5",
              "2016-12-31T23:59:59", "2016-12-31T23:59:59.5",
              "2016-12-31T23:59:60", "2016-12-31T23:59:60.5",
              "2016-12-31T23:59:60.250", "2017-01-01T00:00:00",
              "2017-01-01T00:00:00.5", "2017-01-01T00:00:01"]:
        add_fwd(u, "leap2016")

    # 5. Contemporary (provisional regime) + non-leap rollovers.
    for u in ["2017-06-01T00:00:00", "2018-01-01T00:00:00",
              "2019-12-31T23:59:59", "2020-01-01T00:00:00",
              "2020-02-29T00:00:00", "2020-12-31T23:59:59",
              "2021-01-01T00:00:00", "2021-01-31T23:59:59",
              "2021-02-01T00:00:00", "2022-06-30T23:59:59",
              "2022-07-01T00:00:00", "2023-05-17T08:15:30.125",
              "2024-02-29T23:59:59", "2024-03-01T00:00:00",
              "2025-06-30T23:59:59", "2025-07-01T00:00:00",
              "2026-10-12T00:00:00", "2026-10-11T23:59:59"]:
        add_fwd(u, "contemporary")

    # 6. Fractional seconds.
    for u in ["2005-08-03T04:05:06.789", "1997-06-30T23:59:59.999",
              "2012-06-30T23:59:60.125", "2009-01-01T00:00:00.001",
              "1982-07-01T00:00:00.5", "2015-06-30T23:59:60.750"]:
        add_fwd(u, "fractional")

    # 7. Year / month boundaries.
    for u in ["1989-12-31T23:59:59", "1990-01-01T00:00:00",
              "1999-12-31T23:59:59", "2000-01-01T00:00:00",
              "2009-12-31T23:59:59", "2010-01-01T00:00:00",
              "2016-02-29T12:00:00", "2016-03-01T00:00:00"]:
        add_fwd(u, "boundary")

    # 10. Policy edges + seeded random UTC instants (modern interval).
    for u in ["1972-01-01T00:00:00", "1972-01-01T00:00:01",
              "2017-01-01T00:00:00", "2050-06-15T12:00:00",
              "2100-01-01T00:00:00"]:
        add_fwd(u, "policy")
    rng = np.random.default_rng(SEED)
    base = datetime(1972, 1, 2, tzinfo=timezone.utc)
    span_s = int((datetime(2026, 10, 1, tzinfo=timezone.utc) - base).total_seconds())
    for i in range(60):
        dt = base + timedelta(seconds=int(rng.integers(0, span_s)),
                              milliseconds=int(rng.integers(0, 1000)))
        # Keep off leap-second instants (covered separately); snap :60 risk away.
        if dt.second == 60:
            continue
        add_fwd(dt.strftime("%Y-%m-%dT%H:%M:%S")
                + (f".{dt.microsecond // 1000:03d}" if dt.microsecond else ""),
                "random")

    # 9. Inverse conversions (millisecond grid; exact ISOC strings).
    for u in ["2000-01-01T12:00:00", "2016-12-31T23:59:60",
              "2016-12-31T23:59:59.500", "2017-01-01T00:00:00",
              "1994-06-30T23:59:60", "2024-02-29T12:00:00.250",
              "1972-07-01T00:00:00", "1985-07-01T12:30:15.750"]:
        add_inv(u)
    for i in range(32):
        dt = base + timedelta(seconds=int(rng.integers(0, span_s)),
                              milliseconds=int(rng.integers(0, 1000)))
        add_inv(dt.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3])

    # 8. Invalid leap-second inputs: managed code must REJECT these.
    bad = [
        ("2016-06-30T23:59:60", "no leap announced on 2016-06-30"),
        ("2019-12-31T23:59:60", "no leap announced on 2019-12-31"),
        ("2026-10-11T23:59:60", "no leap announced on 2026-10-11"),
        ("1971-12-31T23:59:60", "pre-1972; no supported leap event"),
        ("2016-12-31T23:59:61", "second 61 never valid"),
        ("2016-12-31T24:00:00", "hour 24 invalid"),
        ("2016-13-01T00:00:00", "month 13 invalid"),
        ("2019-02-29T00:00:00", "2019 not a leap year"),
        ("2016-12-31T23:59:60+00:00", "numeric timezone offsets deferred to a later phase"),
        ("2016-12-31T23:59:60 ", "trailing input / missing Z designator"),
        ("2016-12-31 23:59:60", "missing T/Z strict format"),
        ("2016-12-31T23:59:60Zulu", "trailing characters"),
        ("not-a-date", "not a timestamp"),
        ("", "empty input"),
        ("2016-12-31T23:59", "missing seconds field"),
        ("2016-12-31", "date only, no time field"),
    ]
    for u, reason in bad:
        try:
            et = float(spice.str2et(u)) if u else None
            if u == "":
                raise ValueError("empty")
        except Exception:
            et = None
        invalid.append({"utc": u, "reason": reason, "spiceEtOrNull": et,
                        "managedMustReject": True})

    leap_count = sum(1 for c in forward if c["tag"] in ("leap", "leap2016"))
    meta = {
        "source": "CSPICE str2et/et2utc/deltet with authenticated naif0012.tls (independent of managed converter)",
        "kernelSha256": EXPECTED_KERNEL_SHA256,
        "seed": SEED,
        "forwardCount": len(forward),
        "leapBoundaryCount": leap_count,
        "inverseCount": len(inverse),
        "invalidCount": len(invalid),
        "epochNote": "et values are SPICE ET seconds past J2000 (TDB); deltet is ET-UTC at that ET",
    }
    with open(output, "w") as f:
        json.dump({"metadata": meta, "forward": forward,
                   "inverse": inverse, "invalid": invalid}, f, separators=(",", ":"))
    print(f"forward: {len(forward)} (leap-boundary: {leap_count}) "
          f"inverse: {len(inverse)} invalid: {len(invalid)} -> {output}")
    assert len(forward) >= 150, "fewer than 150 forward comparisons"
    assert leap_count >= 30, "fewer than 30 leap-boundary cases"
    return 0


if __name__ == "__main__":
    sys.exit(main())
