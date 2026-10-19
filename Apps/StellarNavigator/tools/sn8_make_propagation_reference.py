#!/usr/bin/env python3
"""Generate SN8's independent two-body propagation fixture with CSPICE.

HOST TOOLING ONLY. Requires Python and spiceypy/CSPICE N0067. The shipped
managed runtime and xUnit tests consume the JSON fixture and never load Python
or CSPICE. Cases are deterministic and independently propagated with the
NAIF prop2b_c routine; this script does not call the managed implementation.

Usage:
    python sn8_make_propagation_reference.py <output.json>
"""

from __future__ import annotations

import json
import math
import random
import sys

import spiceypy as spice

GM_KM3_S2 = 1.3271244004127942e11
GM_M3_S2 = GM_KM3_S2 * 1.0e9
SEED = 20261018
AU_M = 149_597_870_700.0
YEAR_S = 365.25 * 86400.0
SOURCE_FILE = "gm_de440.tpc"
SOURCE_URL = "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/gm_de440.tpc"
SOURCE_SHA256 = "924ddf4fb9ead9fe8a1aa55780bcabde40b09d00065d58226e24b68d8092f140"


def rotate(vector: tuple[float, float, float], angles: tuple[float, float, float]) -> list[float]:
    x, y, z = vector
    rz1, rx, rz2 = angles
    c, s = math.cos(rz1), math.sin(rz1)
    x, y = c * x - s * y, s * x + c * y
    c, s = math.cos(rx), math.sin(rx)
    y, z = c * y - s * z, s * y + c * z
    c, s = math.cos(rz2), math.sin(rz2)
    x, y = c * x - s * y, s * x + c * y
    return [x, y, z]


def make_case(rng: random.Random, index: int) -> dict[str, object]:
    bucket = index % 4
    angles = (rng.uniform(-math.pi, math.pi), rng.uniform(-math.pi, math.pi),
              rng.uniform(-math.pi, math.pi))
    if bucket == 0:
        regime = "circular"
        radius = rng.uniform(0.3, 25.0) * AU_M
        r = (radius, 0.0, 0.0)
        v = (0.0, math.sqrt(GM_M3_S2 / radius), 0.0)
        period = 2.0 * math.pi * math.sqrt(radius**3 / GM_M3_S2)
        dt = rng.choice((-1.0, 1.0)) * rng.uniform(0.01, min(2.0, 50.0 * YEAR_S / period)) * period
    elif bucket == 1:
        regime = "elliptical"
        semi_major = rng.uniform(0.4, 20.0) * AU_M
        eccentricity = rng.uniform(0.02, 0.92)
        anomaly = rng.uniform(-math.pi, math.pi)
        p = semi_major * (1.0 - eccentricity**2)
        radius = p / (1.0 + eccentricity * math.cos(anomaly))
        speed_scale = math.sqrt(GM_M3_S2 / p)
        r = (radius * math.cos(anomaly), radius * math.sin(anomaly), 0.0)
        v = (-speed_scale * math.sin(anomaly), speed_scale * (eccentricity + math.cos(anomaly)), 0.0)
        period = 2.0 * math.pi * math.sqrt(semi_major**3 / GM_M3_S2)
        dt = rng.choice((-1.0, 1.0)) * rng.uniform(0.005, min(1.5, 50.0 * YEAR_S / period)) * period
    elif bucket == 2:
        regime = "hyperbolic"
        periapsis = rng.uniform(0.2, 12.0) * AU_M
        eccentricity = rng.uniform(1.03, 2.5)
        infinity_anomaly = math.acos(-1.0 / eccentricity)
        anomaly = rng.uniform(-0.8 * infinity_anomaly, 0.8 * infinity_anomaly)
        p = periapsis * (1.0 + eccentricity)
        radius = p / (1.0 + eccentricity * math.cos(anomaly))
        speed_scale = math.sqrt(GM_M3_S2 / p)
        r = (radius * math.cos(anomaly), radius * math.sin(anomaly), 0.0)
        v = (-speed_scale * math.sin(anomaly), speed_scale * (eccentricity + math.cos(anomaly)), 0.0)
        dt = rng.choice((-1.0, 1.0)) * rng.uniform(30_000.0, 8.0 * YEAR_S)
    else:
        regime = "parabolic-near-parabolic"
        periapsis = rng.uniform(0.4, 8.0) * AU_M
        eccentricity = (1.0 if index % 8 == 3 else
                        1.0 + rng.choice((-1.0, 1.0)) * 10.0**rng.uniform(-13.0, -9.0))
        anomaly = rng.uniform(-1.1, 1.1)
        p = periapsis * (1.0 + eccentricity)
        radius = p / (1.0 + eccentricity * math.cos(anomaly))
        speed_scale = math.sqrt(GM_M3_S2 / p)
        r = (radius * math.cos(anomaly), radius * math.sin(anomaly), 0.0)
        v = (-speed_scale * math.sin(anomaly), speed_scale * (eccentricity + math.cos(anomaly)), 0.0)
        dt = rng.choice((-1.0, 1.0)) * rng.uniform(10_000.0, 2.0 * YEAR_S)

    position = rotate(r, angles)
    velocity = rotate(v, angles)
    initial = position + velocity
    propagated = [float(value) for value in spice.prop2b(GM_M3_S2, initial, dt)]
    return {
        "id": f"sn8-{index:03d}",
        "regime": regime,
        "planeAnglesRad": list(angles),
        "initialEtSeconds": 0.0,
        "initialPositionMeters": position,
        "initialVelocityMetersPerSecond": velocity,
        "dtSeconds": dt,
        "expectedPositionMeters": propagated[:3],
        "expectedVelocityMetersPerSecond": propagated[3:],
    }


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <output.json>")
        return 2
    if spice.tkvrsn("toolkit") != "CSPICE_N0067":
        raise RuntimeError(f"Expected CSPICE_N0067, got {spice.tkvrsn('toolkit')}")
    rng = random.Random(SEED)
    cases = [make_case(rng, index) for index in range(200)]
    metadata = {
        "source": "NAIF CSPICE prop2b_c; independent of managed solver",
        "toolkit": spice.tkvrsn("toolkit"),
        "routine": "prop2b_c",
        "comparisonCount": len(cases),
        "seed": SEED,
        "muKm3PerSecond2": GM_KM3_S2,
        "muMeters3PerSecond2": GM_M3_S2,
        "gmSourceFilename": SOURCE_FILE,
        "gmSourceUrl": SOURCE_URL,
        "gmSourceSha256": SOURCE_SHA256,
        "timeScale": "ET seconds past J2000 (TDB)",
        "units": "meters, meters/second, seconds, meters^3/second^2",
        "regimeCounts": {name: sum(case["regime"] == name for case in cases)
                         for name in sorted({case["regime"] for case in cases})},
        "categories": ["circular", "elliptical", "hyperbolic", "parabolic-near-parabolic",
                       "forward and reverse", "rotated planes", "0.2 AU to 25 AU initial scale"],
    }
    with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as out:
        json.dump({"metadata": metadata, "cases": cases}, out, indent=2, allow_nan=False)
        out.write("\n")
    print(f"Wrote {len(cases)} independent CSPICE {metadata['routine']} comparisons")
    print(f"CSPICE toolkit: {metadata['toolkit']}; deterministic seed: {SEED}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
