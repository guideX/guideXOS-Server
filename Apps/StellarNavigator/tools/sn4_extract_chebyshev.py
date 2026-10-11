#!/usr/bin/env python3
"""Stellar Navigator SN4 — deterministic Chebyshev Type-2 extraction to a
compact versioned binary dataset with complete eight-planet coverage.

HOST TOOLING ONLY. Requires Python + spiceypy (CSPICE N0067) + numpy on the
preparation host. The guideXOS runtime must never depend on this script,
Python, CSPICE native binaries, or network access.

Reads genuine SPK Type-2 (Chebyshev position-only) segments from an
authenticated JPL DE440s kernel and writes a self-contained, offline,
indexed binary runtime dataset for the managed lazy-loading evaluator.

Usage:
    python sn4_extract_chebyshev.py <de440s.bsp> <output.sn4bin>
    python sn4_extract_chebyshev.py <de440s.bsp> <output.sn4bin> --verify-against <sn3_chebyshev.json>

The script authenticates the kernel (MD5 + SHA-256 must match the published
values), decodes every segment descriptor, reads raw Type-2 records through
the DAF layer, verifies record geometry (MID/RADIUS vs INIT/INTLEN),
spot-checks raw-record evaluation against independent CSPICE spkezr calls,
writes the binary dataset, re-reads it to prove bit-exact coefficient
round-tripping, and prints the output SHA-256 for the provider manifest.

Binary layout (all integers little-endian; all floats binary64 LE):
    Header:
        magic[8]            = b"SNEPH04B"
        version u32         = 1
        segmentCount u32
        totalRecords u64
        provenanceLength u32 (bytes of UTF-8 JSON provenance block)
        provenance[provenanceLength]
    Segment table (per segment, in ascending target-id order):
        bodyIdLen u32 + bodyId UTF-8
        centerBodyIdLen u32 + centerBodyId UTF-8
        targetId i32, centerId i32, frameId i32, segmentType i32, degree i32
        recordIntervalSeconds f64, initEt f64
        recordCount u64
        coverageStartJd f64, coverageEndJd f64
        dataOffset u64 (absolute file offset of first record word)
        recordLength u64 (binary64 words per record = RSIZE)
    Coefficient data: contiguous f64 records, segment after segment, each
        record [MID, RADIUS, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD] verbatim.

Record layout (SPK Type 2, per NAIF SPK Required Reading):
    [MID, RADIUS, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD]
with RSIZE = 3*(PD+1)+2, u = (t-MID)/RADIUS, position in km, RADIUS in s.
Velocity is obtained by the managed runtime via polynomial differentiation
with chain-rule scaling (1/RADIUS); it is NOT stored separately (Type 2).
"""

from __future__ import annotations

import hashlib
import json
import struct
import sys

import numpy as np
import spiceypy as spice

# Published checksums for de440s.bsp (NAIF aa_checksums.txt + SN2/SN3 record).
EXPECTED_MD5 = "3917ee56769db332790c751e2168843d"
EXPECTED_SHA256 = (
    "c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2"
)

J2000_JD = 2451545.0
SEC_PER_DAY = 86400.0
RETRIEVAL_DATE = "2026-10-11"

MAGIC = b"SNEPH04B"
VERSION = 1

# NAIF target id -> runtime body id. SN4 exports every DE440s segment except
# Pluto barycenter (9), which is out of scope (not a major planet).
# Mercury/Venus body centers (199/299) are genuine single-record degree-1
# segments centered on their system barycenters (1/2); they are exported
# under explicit "_center" labels so the runtime can distinguish measured
# barycenter states from measured planet-center states.
EXPORT_TARGETS = {
    1: "mercury",
    2: "venus",
    3: "emb",
    4: "mars",
    5: "jupiter",
    6: "saturn",
    7: "uranus",
    8: "neptune",
    10: "sun",
    199: "mercury_center",
    299: "venus_center",
    301: "moon",
    399: "earth",
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
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    verify_against = None
    for a in sys.argv[1:]:
        if a.startswith("--verify-against="):
            verify_against = a.split("=", 1)[1]
    if len(args) != 2:
        print(f"usage: {sys.argv[0]} <de440s.bsp> <output.sn4bin> [--verify-against=<sn3.json>]")
        return 2
    kernel, output = args

    print(f"authenticating {kernel} ...")
    with open(kernel, "rb") as f:
        kernel_bytes = f.read()
    md5 = hashlib.md5(kernel_bytes).hexdigest()
    sha256 = hashlib.sha256(kernel_bytes).hexdigest()
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

    print("  kernel segment inventory (all segments):")
    for d in sorted(descriptors, key=lambda x: x["target"]):
        tag = EXPORT_TARGETS.get(d["target"], "(not exported)")
        print(
            f"    target {d['target']:>4} center {d['center']:>4} "
            f"type {d['type']} frame {d['frame']} {tag}"
        )
    if 9 in EXPORT_TARGETS:
        print("FATAL: Pluto (9) must remain out of SN4 scope")
        return 1

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

        if d["center"] != 0 and d["center"] not in EXPORT_TARGETS:
            print(
                f"FATAL: target {d['target']} center {d['center']} "
                "is not exported; center chain would be unresolvable"
            )
            return 1
        print(
            f"  target {d['target']:>3} ({body_id:>14}) center {d['center']} "
            f"type 2 frame J2000 degree {degree:>2} "
            f"intlen {intlen:>12.1f}s nrec {nrec:>6} span "
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

    if verify_against:
        print(f"cross-checking shared segments bit-exactly against {verify_against} ...")
        old = json.load(open(verify_against))
        old_by_target = {s["targetId"]: s for s in old["segments"]}
        for s in segments:
            if s["targetId"] not in old_by_target:
                continue
            o = old_by_target[s["targetId"]]
            assert o["recordCount"] == s["recordCount"], s["bodyId"]
            assert o["degree"] == s["degree"], s["bodyId"]
            assert o["recordIntervalSeconds"] == s["recordIntervalSeconds"], s["bodyId"]
            assert o["initEt"] == s["initEt"], s["bodyId"]
            for r_new, r_old in zip(s["records"], o["records"]):
                assert len(r_new) == len(r_old), s["bodyId"]
                for a, b in zip(r_new, r_old):
                    if a != b:
                        print(f"FATAL: bit mismatch in shared segment {s['bodyId']}")
                        return 1
            print(f"    {s['bodyId']:>14}: {s['recordCount']} records bit-identical to SN3 JSON")
        print("  shared-segment cross-check OK")

    provenance = {
        "source": "NASA/JPL DE440s planetary ephemeris (de440s.bsp)",
        "sourceUrl": "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp",
        "retrievalDate": RETRIEVAL_DATE,
        "spkMd5": md5,
        "spkSha256": sha256,
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
        "format": "SNEPH04B version 1 (indexed binary; see sn4_extract_chebyshev.py header)",
        "centerChains": {
            s["bodyId"]: chain_of(s, segments) for s in segments
        },
        "bodyNotes": {
            "mars": "Mars SYSTEM BARYCENTER (NAIF 4), not Mars center (NAIF 499); 499 is absent from de440s.bsp.",
            "jupiter": "Jupiter SYSTEM BARYCENTER (NAIF 5), not Jupiter center (NAIF 599); 599 is absent from de440s.bsp.",
            "saturn": "Saturn SYSTEM BARYCENTER (NAIF 6), not Saturn center (NAIF 699); 699 is absent from de440s.bsp.",
            "uranus": "Uranus SYSTEM BARYCENTER (NAIF 7), not Uranus center (NAIF 799); 799 is absent from de440s.bsp.",
            "neptune": "Neptune SYSTEM BARYCENTER (NAIF 8), not Neptune center (NAIF 899); 899 is absent from de440s.bsp.",
            "mercury": "Mercury SYSTEM BARYCENTER (NAIF 1). Mercury center (NAIF 199) is a separate exported segment 'mercury_center'.",
            "venus": "Venus SYSTEM BARYCENTER (NAIF 2). Venus center (NAIF 299) is a separate exported segment 'venus_center'.",
            "mercury_center": "Mercury BODY CENTER (NAIF 199) relative to Mercury barycenter (NAIF 1); single degree-1 record spanning full coverage.",
            "venus_center": "Venus BODY CENTER (NAIF 299) relative to Venus barycenter (NAIF 2); single degree-1 record spanning full coverage.",
            "earth": "Earth GEOCENTER (NAIF 399) via Earth-Moon barycenter (NAIF 3), not EMB (NAIF 3) itself.",
            "emb": "Earth-Moon barycenter helper for chain composition; not part of the SN2 supported set.",
            "pluto": "Pluto system barycenter (NAIF 9) exists in de440s.bsp but is out of SN4 scope (not a major planet); queries report InsufficientInformation.",
        },
        "sampleIntervalDays": None,
        "totalRecords": sum(s["recordCount"] for s in segments),
        "note": "Genuine DE440s Type-2 Chebyshev records (midpoint, radius, coefficients) extracted verbatim; no refitting or resampling. Runtime converts km->m on output.",
        "generation": {
            "tool": "sn4_extract_chebyshev.py (host-only; Python + spiceypy/CSPICE N0067)",
            "recordFormat": "[MID_s, RADIUS_s, X_0..X_PD, Y_0..Y_PD, Z_0..Z_PD], coefficients in km",
            "normalization": "u = (t - MID) / RADIUS, valid for u in [-1, 1]",
        },
    }
    prov_bytes = json.dumps(provenance, separators=(",", ":")).encode("utf-8")

    print(f"writing {output} ...")
    with open(output, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", VERSION))
        f.write(struct.pack("<I", len(segments)))
        f.write(struct.pack("<Q", sum(s["recordCount"] for s in segments)))
        f.write(struct.pack("<I", len(prov_bytes)))
        f.write(prov_bytes)
        # Segment table: reserve space, then data.
        table_entries = []
        for s in segments:
            body = s["bodyId"].encode("utf-8")
            center = s["centerBodyId"].encode("utf-8")
            entry = struct.pack("<I", len(body)) + body
            entry += struct.pack("<I", len(center)) + center
            entry += struct.pack(
                "<iiiii",
                s["targetId"],
                s["centerId"],
                s["frameId"],
                s["segmentType"],
                s["degree"],
            )
            entry += struct.pack("<d", s["recordIntervalSeconds"])
            entry += struct.pack("<d", s["initEt"])
            entry += struct.pack("<Q", s["recordCount"])
            entry += struct.pack("<d", s["coverageStartJd"])
            entry += struct.pack("<d", s["coverageEndJd"])
            entry += struct.pack("<Q", 0)  # dataOffset placeholder
            entry += struct.pack("<Q", 2 + 3 * (s["degree"] + 1))  # recordLength
            table_entries.append(entry)
        table_start = f.tell()
        table_size = sum(len(e) for e in table_entries)
        data_start = table_start + table_size
        # Assign offsets.
        cursor = data_start
        final_entries = []
        for s, e in zip(segments, table_entries):
            rlen = 2 + 3 * (s["degree"] + 1)
            # dataOffset field is the last 16 bytes (two u64); patch first of the two.
            patched = e[:-16] + struct.pack("<Q", cursor) + e[-8:]
            final_entries.append(patched)
            cursor += s["recordCount"] * rlen * 8
        for e in final_entries:
            f.write(e)
        assert f.tell() == data_start
        for s in segments:
            for rec in s["records"]:
                f.write(struct.pack("<%dd" % len(rec), *rec))

    # Prove lossless round-trip: re-read every record bit-exactly.
    print("verifying lossless binary round-trip ...")
    with open(output, "rb") as f:
        blob = f.read()
    off = 0
    assert blob[off : off + 8] == MAGIC
    off += 8
    (ver,) = struct.unpack_from("<I", blob, off)
    off += 4
    assert ver == VERSION
    (nseg,) = struct.unpack_from("<I", blob, off)
    off += 4
    assert nseg == len(segments)
    off += 8  # totalRecords
    (prov_len,) = struct.unpack_from("<I", blob, off)
    off += 4
    off += prov_len
    for s in segments:
        (blen,) = struct.unpack_from("<I", blob, off)
        off += 4
        assert blob[off : off + blen].decode("utf-8") == s["bodyId"]
        off += blen
        (clen,) = struct.unpack_from("<I", blob, off)
        off += 4
        off += clen
        off += 20  # 5xi32
        off += 8 + 8  # interval + initEt
        (rc,) = struct.unpack_from("<Q", blob, off)
        off += 8
        assert rc == s["recordCount"]
        off += 8 + 8  # coverage JDs
        (doff,) = struct.unpack_from("<Q", blob, off)
        off += 8
        (rlen,) = struct.unpack_from("<Q", blob, off)
        off += 8
        rlen_int = int(rlen)
        assert rlen_int == 2 + 3 * (s["degree"] + 1)
        for i, rec in enumerate(s["records"]):
            pos = int(doff) + i * rlen_int * 8
            got = struct.unpack_from("<%dd" % rlen_int, blob, pos)
            for a, b in zip(got, rec):
                if a != b:
                    print(f"FATAL: round-trip mismatch in {s['bodyId']} record {i}")
                    return 1
    out_hash = sha256_file(output)
    print(f"  output sha256: {out_hash}")
    print(f"  total records: {sum(s['recordCount'] for s in segments)}")
    print(f"  output size  : {len(blob)} bytes")
    print("extraction OK (record this digest in docs + provider manifest)")
    return 0


def chain_of(seg, segments):
    by_target = {s["targetId"]: s for s in segments}
    chain = [seg["targetId"]]
    center = seg["centerId"]
    while center != 0:
        chain.append(center)
        center = by_target[center]["centerId"]
    chain.append(0)
    return chain


if __name__ == "__main__":
    sys.exit(main())
