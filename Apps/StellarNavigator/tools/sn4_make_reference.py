#!/usr/bin/env python3
"""Stellar Navigator SN4 — independent CSPICE verification fixture for the
five newly supported planets (plus Mercury/Venus body centers).

HOST TOOLING ONLY (Python + spiceypy/CSPICE N0067). Uses *only* the
high-level CSPICE spkezr API on the authenticated DE440s kernel. It never
touches raw DAF words, Chebyshev records, the binary dataset, or the
managed evaluator, so the fixture is a genuinely independent reference
for the SN4 lazy-loading runtime.

Usage:
    python sn4_make_reference.py <de440s.bsp> <naif0012.tls> <output.json>

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

Deterministic: fixed seed 20261012. Random cases are uniform off-grid ETs.
Requires at least 50 off-grid cases per newly supported planet.
"""

from __future__ import annotations

import hashlib
import json
import sys

import numpy as np
import spiceypy as spice

SEED = 20261012
J2000_JD = 2451545.0
SEC_PER_DAY = 86400.0
INIT_ET = -4734072000.0
END_ET = 4735368000.0

# (target NAIF, observer NAIF, runtime body label, observer label)
NEW_PLANET_SSB_CASES = [
    (1, 0, "mercury", "ssb"),
    (2, 0, "venus", "ssb"),
    (6, 0, "saturn", "ssb"),
    (7, 0, "uranus", "ssb"),
    (8, 0, "neptune", "ssb"),
]
CENTER_SSB_CASES = [
    (199, 0, "mercury_center", "ssb"),
    (299, 0, "venus_center", "ssb"),
]
CENTER_REL_CASES = [
    (199, 1, "mercury_center", "mercury"),
    (299, 2, "venus_center", "venus"),
]
NEW_PLANET_REL_CASES = [
    (1, 399, "mercury", "earth"),
    (2, 399, "venus", "earth"),
    (6, 399, "saturn", "earth"),
    (7, 399, "uranus", "earth"),
    (8, 399, "neptune", "earth"),
]

# Record junctions to probe: (segment target, record interval s, junction indices)
# INTLENs verified against sn4 extraction output (degrees/intervals printed there).
BOUNDARY_SEGS = [
    (1, 691200.0, [1, 6850, 13699]),
    (2, 1382400.0, [1, 3425, 6849]),
    (6, 2764800.0, [1, 1712, 3424]),
    (7, 2764800.0, [1, 1712, 3424]),
    (8, 2764800.0, [1, 1712, 3424]),
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
    for t, o, b, ob in NEW_PLANET_SSB_CASES + CENTER_SSB_CASES + NEW_PLANET_REL_CASES + CENTER_REL_CASES:
        add("j2000", t, o, b, ob, 0.0)

    # 2. Deterministic off-grid random epochs (fixed seed).
    # At least 50 per newly supported planet (SSB-relative).
    rng = np.random.default_rng(SEED)
    per_planet = 50
    for t, o, b, ob in NEW_PLANET_SSB_CASES:
        raw_ets = rng.uniform(INIT_ET + 3600.0, END_ET - 3600.0, per_planet)
        for raw_et in raw_ets:
            add("random_offgrid", t, o, b, ob, float(raw_et))
    # Earth-relative off-grid pairs (10 epochs per new planet).
    for t, o, b, ob in NEW_PLANET_REL_CASES:
        raw_ets = rng.uniform(INIT_ET + 3600.0, END_ET - 3600.0, 10)
        for raw_et in raw_ets:
            add("random_offgrid", t, o, b, ob, float(raw_et))
    # Body-center cases: SSB-relative + relative-to-own-barycenter.
    for t, o, b, ob in CENTER_SSB_CASES + CENTER_REL_CASES:
        raw_ets = rng.uniform(INIT_ET + 3600.0, END_ET - 3600.0, 25)
        for raw_et in raw_ets:
            add("random_offgrid", t, o, b, ob, float(raw_et))

    # 3. First / last supported epochs.
    for raw_et, tag in ((INIT_ET, "first"), (END_ET, "last")):
        for t, o, b, ob in NEW_PLANET_SSB_CASES + CENTER_SSB_CASES:
            add(tag, t, o, b, ob, raw_et)
    for t, o, b, ob in NEW_PLANET_REL_CASES + CENTER_REL_CASES:
        add("first", t, o, b, ob, INIT_ET)
        add("last", t, o, b, ob, END_ET)

    # 4. Polynomial-record boundary adjacency (exact junction + offsets).
    for target, intlen, js in BOUNDARY_SEGS:
        for j in js:
            junction = INIT_ET + j * intlen
            for off in BOUNDARY_OFFSETS:
                add("boundary", target, 0, _label(target), "ssb", junction + off)
    # Single-record center segments: mid-coverage probes.
    for target in (199, 299):
        for frac in (0.25, 0.5, 0.75):
            add("boundary", target, 0, _label(target), "ssb",
                INIT_ET + frac * (END_ET - INIT_ET))

    meta = {
        "source": "CSPICE spkezr on authenticated de440s.bsp (independent of SN4 managed evaluator)",
        "kernelMd5": md5,
        "seed": SEED,
        "count": len(cases),
        "offgridCount": sum(1 for c in cases if c["case"] == "random_offgrid"),
        "offgridPerNewPlanet": per_planet,
        "frame": "J2000",
        "aberrationCorrection": "NONE",
        "units": {"position": "km", "velocity": "km/s"},
        "epochNote": "jdTdb is the JD-double API epoch; etSec is the reconstructed ET at which CSPICE was evaluated",
    }
    with open(output, "w") as f:
        json.dump({"metadata": meta, "cases": cases}, f, separators=(",", ":"))
    print(f"cases: {len(cases)} (off-grid: {meta['offgridCount']}) -> {output}")
    per_body = {}
    for c in cases:
        if c["case"] == "random_offgrid" and c["observerId"] == "ssb" and c["bodyId"] in (
            "mercury", "venus", "saturn", "uranus", "neptune",
        ):
            per_body[c["bodyId"]] = per_body.get(c["bodyId"], 0) + 1
    print(f"  off-grid SSB-relative per new planet: {per_body}")
    assert all(v >= 50 for v in per_body.values()), "fewer than 50 off-grid cases for a new planet"
    return 0


def _label(target: int) -> str:
    return {
        1: "mercury",
        2: "venus",
        6: "saturn",
        7: "uranus",
        8: "neptune",
        199: "mercury_center",
        299: "venus_center",
    }[target]


if __name__ == "__main__":
    sys.exit(main())
