#!/usr/bin/env python3
"""Stellar Navigator SN3 — independent CSPICE verification fixture.

HOST TOOLING ONLY (Python + spiceypy/CSPICE N0067). Uses *only* the
high-level CSPICE spkezr API on the authenticated DE440s kernel. It never
touches raw DAF words, Chebyshev records, or the managed evaluator, so the
fixture is a genuinely independent reference for the SN3 runtime.

Usage:
    python sn3_make_reference.py <de440s.bsp> <naif0012.tls> <output.json>

Epoch handling (read carefully): the runtime provider receives epochs as
Julian-Date doubles (AstroTime) and reconstructs ET = (JD-2451545)*86400.
A JD double near 2.5e6 quantizes time to ~80 microseconds, which would
dominate a millimetre-level comparison. Every case below is therefore built
as: raw ET -> JD double -> reconstructed ET, and the CSPICE reference is
evaluated at the *reconstructed* ET — exactly the epoch the managed runtime
will reconstruct from the same JD. This keeps the comparison honest without
hiding provider error (record selection, evaluation, and chaining are all
still exercised; only sub-microsecond epoch-input quantization is factored
out, and it is documented as a property of the JD-double API).

Deterministic: fixed seed 20261011. Random cases are uniform off-grid ETs.
"""

from __future__ import annotations

import hashlib
import json
import sys

import numpy as np
import spiceypy as spice

SEED = 20261011
J2000_JD = 2451545.0
SEC_PER_DAY = 86400.0
INIT_ET = -4734072000.0
END_ET = 4735368000.0

# (target NAIF, observer NAIF, runtime body label, observer label)
SSB_CASES = [
    (10, 0, "sun", "ssb"),
    (399, 0, "earth", "ssb"),
    (301, 0, "moon", "ssb"),
    (4, 0, "mars", "ssb"),
    (5, 0, "jupiter", "ssb"),
]
REL_CASES = [
    (301, 399, "moon", "earth"),
    (4, 399, "mars", "earth"),
    (5, 399, "jupiter", "earth"),
]

# Record junctions to probe: (segment target, record interval s, junction indices)
BOUNDARY_SEGS = [
    (301, 345600.0, [1, 13700, 27399]),
    (399, 345600.0, [1, 27399]),
    (10, 1382400.0, [1, 6849]),
    (5, 2764800.0, [1, 3424]),
    (4, 2764800.0, [3424]),
    (3, 1382400.0, [6849]),
]
BOUNDARY_OFFSETS = [0.0, -0.001, 0.001, -0.5, 0.5]


def quantize_et(raw_et: float):
    """Round-trip a raw ET through a JD double, as the runtime API does."""
    jd = J2000_JD + raw_et / SEC_PER_DAY
    et = (jd - J2000_JD) * SEC_PER_DAY
    return jd, et


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <de440s.bsp> <naif0012.tls> <output.json>")
        return 2
    kernel, tls, output = sys.argv[1], sys.argv[2], sys.argv[3]

    md5 = hashlib.md5(open(kernel, "rb").read()).hexdigest()
    assert md5 == "3917ee56769db332790c751e2168843d", "kernel not authenticated"

    spice.furnsh(kernel)
    spice.furnsh(tls)

    cases = []

    def add(case: str, target: int, observer: int, body: str, obs: str, raw_et: float):
        jd, et = quantize_et(raw_et)
        state, _ = spice.spkezr(
            str(target), et, "J2000", "NONE", str(observer)
        )
        cases.append(
            {
                "case": case,
                "bodyId": body,
                "observerId": obs,
                "targetNaif": target,
                "observerNaif": observer,
                "jdTdb": jd,
                "etSec": et,
                "posKm": [float(state[0]), float(state[1]), float(state[2])],
                "velKms": [float(state[3]), float(state[4]), float(state[5])],
            }
        )

    # 1. J2000.
    for t, o, b, ob in SSB_CASES + REL_CASES:
        add("j2000", t, o, b, ob, 0.0)

    # 2. Existing SN2 sample epochs (30-day grid anchored at J2000).
    for k in (-1000, -333, -7, 7, 333, 1000, 1500):
        raw_et = float(k) * 30.0 * SEC_PER_DAY
        for t, o, b, ob in SSB_CASES:
            add("sn2_grid", t, o, b, ob, raw_et)
    for t, o, b, ob in REL_CASES:
        add("sn2_grid", t, o, b, ob, 333.0 * 30.0 * SEC_PER_DAY)

    # 3. Deterministic off-grid random epochs (fixed seed).
    rng = np.random.default_rng(SEED)
    raw_ets = rng.uniform(INIT_ET + 3600.0, END_ET - 3600.0, 120)
    bodies = [SSB_CASES[i % len(SSB_CASES)] for i in range(len(raw_ets))]
    for raw_et, (t, o, b, ob) in zip(raw_ets, bodies):
        add("random_offgrid", t, o, b, ob, float(raw_et))
    # Relative-pair off-grid cases on a subset of the same epochs.
    for raw_et, (t, o, b, ob) in zip(raw_ets[:30], REL_CASES * 10):
        add("random_offgrid", t, o, b, ob, float(raw_et))

    # 4. First / last supported epochs.
    for raw_et, tag in ((INIT_ET, "first"), (END_ET, "last")):
        for t, o, b, ob in SSB_CASES:
            add(tag, t, o, b, ob, raw_et)
    for t, o, b, ob in REL_CASES:
        add("first", t, o, b, ob, INIT_ET)
        add("last", t, o, b, ob, END_ET)

    # 5. Polynomial-record boundary adjacency (exact junction + offsets).
    for target, intlen, js in BOUNDARY_SEGS:
        for j in js:
            junction = INIT_ET + j * intlen
            for off in BOUNDARY_OFFSETS:
                add("boundary", target, 0, _label(target), "ssb", junction + off)

    meta = {
        "source": "CSPICE spkezr on authenticated de440s.bsp (independent of SN3 managed evaluator)",
        "kernelMd5": md5,
        "seed": SEED,
        "count": len(cases),
        "offgridCount": sum(1 for c in cases if c["case"] == "random_offgrid"),
        "frame": "J2000",
        "aberrationCorrection": "NONE",
        "units": {"position": "km", "velocity": "km/s"},
        "epochNote": "jdTdb is the JD-double API epoch; etSec is the reconstructed ET at which CSPICE was evaluated",
    }
    with open(output, "w") as f:
        json.dump({"metadata": meta, "cases": cases}, f, separators=(",", ":"))
    print(f"cases: {len(cases)} (off-grid: {meta['offgridCount']}) -> {output}")
    return 0


def _label(target: int) -> str:
    return {10: "sun", 399: "earth", 301: "moon", 4: "mars", 5: "jupiter", 3: "emb"}[
        target
    ]


if __name__ == "__main__":
    sys.exit(main())
