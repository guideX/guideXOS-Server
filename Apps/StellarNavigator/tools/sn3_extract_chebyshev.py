#!/usr/bin/env python3
"""Stellar Navigator SN3 — deterministic Chebyshev Type-2 record extraction.

HOST TOOLING ONLY. Requires Python + spiceypy (CSPICE N0067) on the
preparation host. The guideXOS runtime must never depend on this script,
Python, CSPICE native binaries, or network access.

Reads genuine SPK Type-2 (Chebyshev position-only) segments from an
authenticated JPL DE440s kernel and writes a compact, self-contained,
offline runtime dataset of polynomial records for the managed evaluator.

Usage:
    python sn3_extract_chebyshev.py <de440s.bsp> <output.json>

The script authenticates the kernel (MD5 + SHA-256 must match the published
values), decodes every segment descriptor, reads raw Type-2 records through
the DAF layer, verifies record geometry (MID/RADIUS vs INIT/INTLEN),
spot-checks raw-record evaluation against independent CSPICE spkezr calls,
writes the dataset with losslessly round-trippable float formatting, and
re-reads the written file to prove bit-exact coefficient round-tripping.

Record layout (SPK Type 2, per NAIF SPK Required Reading):
    [MID, RADIUS, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD]
with RSIZE = 3*(PD+1)+2, u = (t-MID)/RADIUS, position in km, RADIUS in s.
Velocity is obtained by the managed runtime via polynomial differentiation
with chain-rule scaling (1/RADIUS); it is NOT stored separately (Type 2).
"""

from __future__ import annotations

import hashlib
import json
import sys

import numpy as np
import spiceypy as spice

# Published checksums for de440s.bsp (NAIF aa_checksums.txt + SN2 record).
EXPECTED_MD5 = "3917ee56769db332790c751e2168843d"
EXPECTED_SHA256 = (
    "c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2"
)

J2000_JD = 2451545.0
SEC_PER_DAY = 86400.0
RETRIEVAL_DATE = "2026-10-11"

# NAIF target id -> runtime body id for the SN3 supported set (plus the
# intermediate Earth-Moon barycenter needed for center-chain composition).
EXPORT_TARGETS = {
    10: "sun",
    3: "emb",
    301: "moon",
    399: "earth",
    4: "mars",
    5: "jupiter",
}

SPOT_CHECK_TOL_KM = 1e-6


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def clenshaw(coeffs, u: float) -> float:
    b2 = 0.0
    b1 = 0.0
    items = list(coeffs)
    for c in reversed(items[1:]):
        b0 = float(c) + 2.0 * u * b1 - b2
        b2, b1 = b1, b0
    return float(items[0]) + u * b1 - b2


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <de440s.bsp> <output.json>")
        return 2
    kernel, output = sys.argv[1], sys.argv[2]

    print(f"authenticating {kernel} ...")
    md5 = hashlib.md5(open(kernel, "rb").read()).hexdigest()
    sha256 = sha256_file(kernel)
    print(f"  md5    : {md5}")
    print(f"  sha256 : {sha256}")
    if md5 != EXPECTED_MD5:
        print(f"FATAL: MD5 mismatch (expected {EXPECTED_MD5})")
        return 1
    if sha256 != EXPECTED_SHA256:
        print(f"FATAL: SHA-256 mismatch (expected {EXPECTED_SHA256})")
        return 1
    print("  kernel authenticated")

    spice.furnsh(kernel)
    handle = spice.dafopr(kernel)

    spice.dafbfs(handle)
    descriptors = []
    while spice.daffna():
        summary = spice.dafgs()[:5]
        target, center, frame, stype, first, last, baddr, eaddr = spice.spkuds(
            summary
        )
        descriptors.append(
            {
                "target": target,
                "center": center,
                "frame": frame,
                "type": stype,
                "first": first,
                "last": last,
                "baddr": baddr,
                "eaddr": eaddr,
            }
        )

    segments = []
    worst_mid_dev = 0.0
    worst_rad_dev = 0.0
    for d in sorted(descriptors, key=lambda x: x["target"]):
        if d["target"] not in EXPORT_TARGETS:
            continue
        if d["type"] != 2:
            print(
                f"FATAL: target {d['target']} segment type {d['type']} "
                "(only Type 2 supported)"
            )
            return 1
        if d["frame"] != 1:
            print(
                f"FATAL: target {d['target']} frame {d['frame']} "
                "(only J2000/frame 1 supported)"
            )
            return 1
        eaddr, baddr = d["eaddr"], d["baddr"]
        trailer = np.asarray(spice.dafgda(handle, eaddr - 3, eaddr))
        init, intlen, rsize, nrec = (
            float(trailer[0]),
            float(trailer[1]),
            int(trailer[2]),
            int(trailer[3]),
        )
        degree = (rsize - 2) // 3 - 1
        if 3 * (degree + 1) + 2 != rsize:
            print(f"FATAL: target {d['target']} bad RSIZE {rsize}")
            return 1
        size = eaddr - baddr + 1
        if size != nrec * rsize + 4:
            print(
                f"FATAL: target {d['target']} size {size} != "
                f"nrec*rsize+4 = {nrec * rsize + 4}"
            )
            return 1
        words = np.asarray(spice.dafgda(handle, baddr, eaddr - 4))
        if len(words) != nrec * rsize:
            print(f"FATAL: short read for target {d['target']}")
            return 1

        records = []
        nc = degree + 1
        for i in range(nrec):
            rec = words[i * rsize : (i + 1) * rsize]
            mid, radius = float(rec[0]), float(rec[1])
            exp_mid = init + (i + 0.5) * intlen
            exp_rad = intlen / 2.0
            worst_mid_dev = max(worst_mid_dev, abs(mid - exp_mid))
            worst_rad_dev = max(worst_rad_dev, abs(radius - exp_rad))
            if abs(mid - exp_mid) > 1e-6 or abs(radius - exp_rad) > 1e-9:
                print(
                    f"FATAL: target {d['target']} record {i} geometry "
                    f"deviation mid={mid - exp_mid:.3e} rad={radius - exp_rad:.3e}"
                )
                return 1
            records.append([float(v) for v in rec])

        # Spot-check raw-record evaluation against independent spkezr.
        body_id = EXPORT_TARGETS[d["target"]]
        for k in (0, nrec // 2, nrec - 1):
            rec = records[k]
            mid, radius = rec[0], rec[1]
            for frac in (0.0, 0.25):
                et = mid + frac * radius
                u = (et - mid) / radius
                got = np.array(
                    [
                        clenshaw(rec[2 : 2 + nc], u),
                        clenshaw(rec[2 + nc : 2 + 2 * nc], u),
                        clenshaw(rec[2 + 2 * nc : 2 + 3 * nc], u),
                    ]
                )
                ref, _ = spice.spkezr(
                    str(d["target"]), et, "J2000", "NONE", str(d["center"])
                )
                err = float(np.max(np.abs(got - np.array(ref[:3]))))
                if err > SPOT_CHECK_TOL_KM:
                    print(
                        f"FATAL: spot check {body_id} rec {k} err {err:.3e} km"
                    )
                    return 1

        print(
            f"  target {d['target']:>3} ({body_id:>4}) center {d['center']} "
            f"type 2 frame J2000 degree {degree:>2} "
            f"intlen {intlen:>10.1f}s nrec {nrec:>6} span "
            f"[{J2000_JD + init / SEC_PER_DAY:.2f}, "
            f"{J2000_JD + (init + nrec * intlen) / SEC_PER_DAY:.2f}] JD"
        )
        segments.append(
            {
                "bodyId": body_id,
                "targetId": d["target"],
                "centerId": d["center"],
                "centerBodyId": EXPORT_TARGETS.get(d["center"], "ssb")
                if d["center"] != 0
                else "ssb",
                "frameId": d["frame"],
                "frame": "J2000",
                "segmentType": 2,
                "degree": degree,
                "recordIntervalSeconds": intlen,
                "initEt": init,
                "recordCount": nrec,
                "coverageStartJd": J2000_JD + init / SEC_PER_DAY,
                "coverageEndJd": J2000_JD
                + (init + nrec * intlen) / SEC_PER_DAY,
                "records": records,
            }
        )

    spice.dafcls(handle)
    print(f"  worst MID deviation   : {worst_mid_dev:.3e} s")
    print(f"  worst RADIUS deviation: {worst_rad_dev:.3e} s")

    metadata = {
        "source": "NASA/JPL DE440s planetary ephemeris (de440s.bsp)",
        "sourceUrl": "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp",
        "retrievalDate": RETRIEVAL_DATE,
        "spkMd5": md5,
        "spkSha256": sha256,
        "outputSha256": "",
        "coverage": {
            "start": "1849-08-28T00:00:00Z (approx; see segment coverage)",
            "end": "2150-08-01T00:00:00Z (approx; see segment coverage)",
        },
        "frame": "J2000 (ICRF-compatible inertial axes; SPK frame id 1)",
        "origin": "Solar System Barycenter (NAIF ID 0) via center chains",
        "timeScale": "TDB (Barycentric Dynamical Time); epochs are ET seconds past J2000 (JD 2451545.0 TDB)",
        "units": {"position": "km", "velocity": "km/s (derived by differentiation)"},
        "aberrationCorrection": "NONE (geometric states)",
        "segmentType": 2,
        "segmentTypeNote": "Chebyshev position-only; velocity obtained by differentiating the position polynomial with chain-rule scaling 1/RADIUS. No Type 3 (separate velocity polynomials) segments are present in de440s.bsp.",
        "centerChains": {
            "sun": [10, 0],
            "emb": [3, 0],
            "moon": [301, 3, 0],
            "earth": [399, 3, 0],
            "mars": [4, 0],
            "jupiter": [5, 0],
        },
        "bodyNotes": {
            "mars": "Mars SYSTEM BARYCENTER (NAIF 4), not Mars center (NAIF 499); 499 is absent from de440s.bsp.",
            "earth": "Earth GEOCENTER (NAIF 399) via Earth-Moon barycenter (NAIF 3), not EMB (NAIF 3) itself.",
            "emb": "Earth-Moon barycenter helper for chain composition; not part of the SN2 supported set.",
        },
        "sampleIntervalDays": None,
        "totalRecords": sum(s["recordCount"] for s in segments),
        "note": "Genuine DE440s Type-2 Chebyshev records (midpoint, radius, coefficients) extracted verbatim; no refitting or resampling. Runtime converts km->m on output.",
        "generation": {
            "tool": "sn3_extract_chebyshev.py (host-only; Python + spiceypy/CSPICE N0067)",
            "recordFormat": "[MID_s, RADIUS_s, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD], coefficients in km",
            "normalization": "u = (t - MID) / RADIUS, valid for u in [-1, 1]",
        },
    }
    dataset = {"metadata": metadata, "segments": segments}

    print(f"writing {output} ...")
    with open(output, "w") as f:
        json.dump(dataset, f, separators=(",", ":"), sort_keys=False)

    # Prove lossless round-trip: re-read and compare bit-exactly.
    print("verifying lossless JSON round-trip ...")
    back = json.load(open(output))
    assert len(back["segments"]) == len(segments)
    for s_new, s_old in zip(back["segments"], segments):
        assert s_new["recordCount"] == s_old["recordCount"]
        for r_new, r_old in zip(s_new["records"], s_old["records"]):
            assert len(r_new) == len(r_old)
            for a, b in zip(r_new, r_old):
                if a != b:
                    print("FATAL: round-trip mismatch")
                    return 1
    out_hash = sha256_file(output)
    print(f"  output sha256: {out_hash}")
    print(f"  total records: {metadata['totalRecords']}")
    print("extraction OK (record this digest in docs + provider manifest)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
