#!/usr/bin/env python3
"""Stellar Navigator SN6 -- independent CSPICE frame-transform reference fixture.

HOST TOOLING ONLY (Python + spiceypy/CSPICE N0067 loaded with the SAME
authenticated text PCK the runtime model was derived from). Expected values
come exclusively from pxform/sxform -- never from the managed code under test.

Usage:
    python sn6_make_reference.py <pck00011.tpc> <output.json>

Coverage (deterministic, seed 20261013):
    1. J2000 (et=0).
    2. Historical epochs (1905, 1952, 1984 scales).
    3. Modern epochs (2003, 2015, 2025 scales).
    4. Earth-rotation phases within one sidereal day of J2000.
    5. Shipped-dataset coverage boundaries (+/-4756320000 s, JD 2396495/2506595).
    6. 100 deterministic off-grid epochs uniform in [-4e9, +4e9] s.

For every epoch the fixture stores the pxform rotation R and the sxform
derivative block Rdot (lower-left 3x3). Representative state vectors are
stored once globally (Earth-relative, spacecraft-relative, surface-scale
stationary point, zero edge case); tests form epoch x vector comparisons
(113 epochs x 4 vectors = 452 state-transform comparisons, 400 off-grid).
"""

from __future__ import annotations

import hashlib
import json
import sys

import numpy as np
import spiceypy as spice

SEED = 20261013
EXPECTED_PCK_SHA256 = "3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1"

EPOCHS: list[tuple[float, str]] = [
    (0.0, "j2000"),
    (-3000000000.0, "historical"),
    (-1500000000.0, "historical"),
    (-500000000.0, "historical"),
    (100000000.0, "modern"),
    (500000000.0, "modern"),
    (800000000.0, "modern"),
    (0.0, "rotation_phase"),
    (21600.0, "rotation_phase"),
    (43200.0, "rotation_phase"),
    (64800.0, "rotation_phase"),
    (-4756320000.0, "boundary"),
    (4756320000.0, "boundary"),
]

VECTORS = [
    {"tag": "earth_relative",
     "r": [1.5e11, 2.0e10, 5.0e9],
     "v": [30000.0, 10000.0, 5000.0]},
    {"tag": "spacecraft_relative",
     "r": [1.0e9, -2.0e9, 3.0e8],
     "v": [5000.0, -2000.0, 1000.0]},
    {"tag": "surface_stationary",
     "r": [6378136.6, 0.0, 0.0],
     "v": [0.0, 0.0, 0.0]},
    {"tag": "zero",
     "r": [0.0, 0.0, 0.0],
     "v": [0.0, 0.0, 0.0]},
]


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <pck00011.tpc> <output.json>")
        return 2
    pck, output = sys.argv[1], sys.argv[2]

    sha = hashlib.sha256(open(pck, "rb").read()).hexdigest()
    assert sha == EXPECTED_PCK_SHA256, f"PCK not authenticated: {sha}"
    spice.kclear()
    spice.furnsh(pck)
    assert spice.tkvrsn("toolkit") == "CSPICE_N0067", spice.tkvrsn("toolkit")
    # Sanity: J2000 rotation matches the closed-form Rz(79.853 deg) expectation.
    r0 = np.array(spice.pxform("J2000", "IAU_EARTH", 0.0))
    assert abs(r0[0, 0] - 0.176174259633) < 1e-9, r0

    rng = np.random.default_rng(SEED)
    epochs = list(EPOCHS)
    for et in rng.uniform(-4.0e9, 4.0e9, size=100):
        epochs.append((float(et), "offgrid"))

    cases = []
    for et, tag in epochs:
        r = np.array(spice.pxform("J2000", "IAU_EARTH", et))
        x = np.array(spice.sxform("J2000", "IAU_EARTH", et))
        assert np.allclose(x[0:3, 0:3], r, atol=0.0), "sxform/pxform disagree"
        rdot = x[3:6, 0:3]
        states = []
        for vec in VECTORS:
            rv = np.array(vec["r"], dtype=float)
            vv = np.array(vec["v"], dtype=float)
            state = np.concatenate([rv, vv])
            out = x @ state
            states.append({
                "tag": vec["tag"],
                "rJ2000": [float(v) for v in rv],
                "vJ2000": [float(v) for v in vv],
                "rEarth": [float(v) for v in out[0:3]],
                "vEarth": [float(v) for v in out[3:6]],
            })
        # Inverse check input: IAU_EARTH -> J2000 at the same epoch.
        xinv = np.array(spice.sxform("IAU_EARTH", "J2000", et))
        cases.append({
            "et": float(et),
            "tag": tag,
            "R": [[float(v) for v in row] for row in r],
            "Rdot": [[float(v) for v in row] for row in rdot],
            "RinvCheck": [[float(v) for v in row] for row in np.array(xinv[0:3, 0:3])],
            "states": states,
        })

    spice.kclear()
    offgrid = sum(1 for c in cases if c["tag"] == "offgrid")
    total = len(cases) * len(VECTORS)
    meta = {
        "source": "CSPICE pxform/sxform (N0067) with authenticated pck00011.tpc; independent of managed implementation",
        "pckFilename": "pck00011.tpc",
        "pckUrl": "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc",
        "pckSha256": EXPECTED_PCK_SHA256,
        "toolkit": "CSPICE_N0067",
        "seed": SEED,
        "epochCount": len(cases),
        "offgridEpochs": offgrid,
        "vectorsPerEpoch": len(VECTORS),
        "stateComparisons": total,
        "offgridComparisons": offgrid * len(VECTORS),
        "epochNote": "et values are SPICE ET seconds past J2000 (TDB)",
        "vectorNote": "SI units (m, m/s); r/v expected outputs are sxform applied to inputs",
    }
    with open(output, "w") as f:
        json.dump({"metadata": meta, "vectors": VECTORS, "cases": cases}, f)
    print(f"epochs: {len(cases)} (off-grid: {offgrid}) "
          f"comparisons: {total} (off-grid: {offgrid * len(VECTORS)}) -> {output}")
    assert len(cases) >= 110, "too few epochs"
    assert offgrid >= 100, "fewer than 100 off-grid epochs"
    assert total >= 150, "fewer than 150 state-transform comparisons"
    return 0


if __name__ == "__main__":
    sys.exit(main())
