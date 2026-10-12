#!/usr/bin/env python3
"""Stellar Navigator SN5 — derive the offline runtime leap-second asset from
the official NASA NAIF leapseconds kernel (LSK).

HOST TOOLING ONLY. The offline managed runtime never parses SPICE text
kernels; it loads only the compact deterministic asset written here.

Usage:
    python sn5_extract_leapseconds.py <naif00XX.tls> <output.sn5json>

Steps:
    1. Read the LSK bytes; record SHA-256 (trusted provenance).
    2. Narrowly parse DELTET/DELTA_T_A, DELTET/K, DELTET/EB, DELTET/M and the
       DELTET/DELTA_AT table (effective UTC date + TAI-UTC offset).
    3. Validate: ordered non-duplicated dates, strictly +1 offset steps,
       required constants present with finite values.
    4. Emit a deterministic compact JSON asset (sorted keys, no whitespace
       variance) carrying source provenance + integrity metadata.
    5. Print the asset SHA-256 to embed as the trusted digest in the
       managed provider (source-anchored integrity, same model as SN4).

Supported schema: SN-TIME-01 (offsets as integers, dates as UTC midnights).
A general-purpose SPICE text-kernel parser is deliberately NOT implemented.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from datetime import date

SCHEMA = "SN-TIME-01"

_MONTHS = {
    "JAN": 1, "FEB": 2, "MAR": 3, "APR": 4, "MAY": 5, "JUN": 6,
    "JUL": 7, "AUG": 8, "SEP": 9, "OCT": 10, "NOV": 11, "DEC": 12,
}


def parse_spice_date(token: str) -> str:
    """'@1972-JAN-1' -> '1972-01-01' (validated UTC-midnight calendar date)."""
    m = re.fullmatch(r"@(\d{4})-([A-Z]{3})-(\d{1,2})", token.strip())
    if not m:
        raise ValueError(f"bad DELTA_AT date token: {token!r}")
    year, mon, day = int(m.group(1)), _MONTHS.get(m.group(2)), int(m.group(3))
    if mon is None:
        raise ValueError(f"bad month in token: {token!r}")
    d = date(year, mon, day)  # raises on invalid calendar dates
    return d.isoformat()


def parse_float_d(token: str) -> float:
    """Parse a SPICE 'D'-exponent float (e.g. '1.657D-3')."""
    t = token.strip().replace("D", "E").replace("d", "E")
    v = float(t)
    if v != v or v in (float("inf"), float("-inf")):
        raise ValueError(f"non-finite constant: {token!r}")
    return v


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <naif00XX.tls> <output.sn5json>")
        return 2
    src, output = sys.argv[1], sys.argv[2]

    raw = open(src, "rb").read()
    kernel_sha256 = hashlib.sha256(raw).hexdigest()
    text = raw.decode("ascii")  # LSK files are plain ASCII

    body = text[text.index("\\begindata"):text.index("\\begintext")]

    def grab(name: str) -> str:
        m = re.search(re.escape(name) + r"\s*=\s*(\(.+?\)|.+)",
                      body, re.DOTALL)
        if not m:
            raise ValueError(f"required item missing: {name}")
        return m.group(1).strip()

    delta_t_a = parse_float_d(grab("DELTET/DELTA_T_A").split()[0])
    k = parse_float_d(grab("DELTET/K").split()[0])
    eb = parse_float_d(grab("DELTET/EB").split()[0])
    m_raw = grab("DELTET/M")
    m_vals = re.findall(r"[+-]?\d+(?:\.\d+)?[Dd][+-]?\d+", m_raw)
    if len(m_vals) != 2:
        raise ValueError(f"DELTET/M must hold (M0 M1); got: {m_raw!r}")
    m0, m1 = (parse_float_d(v) for v in m_vals)

    at_raw = grab("DELTET/DELTA_AT")
    pairs = re.findall(r"(\d+)\s*,\s*(@\d{4}-[A-Z]{3}-\d{1,2})", at_raw)
    if not pairs:
        raise ValueError("DELTA_AT table empty or unparsable")
    transitions = [
        {"utc": parse_spice_date(tok) + "T00:00:00Z", "offset": int(off)}
        for off, tok in pairs
    ]

    # ---- validation (ordered, non-duplicated, +1 steps) ----
    seen = set()
    prev_date, prev_off = "", None
    for t in transitions:
        if t["utc"] in seen:
            raise ValueError(f"duplicated transition: {t['utc']}")
        seen.add(t["utc"])
        if t["utc"] <= prev_date:
            raise ValueError("transitions not strictly ordered")
        if prev_off is not None and t["offset"] != prev_off + 1:
            raise ValueError(
                f"offset step must be exactly +1: {prev_off} -> {t['offset']}"
            )
        prev_date, prev_off = t["utc"], t["offset"]
    if transitions[0]["utc"] != "1972-01-01T00:00:00Z":
        raise ValueError("first transition must be 1972-01-01 (LSK history anchor)")
    if not (0.0 < k < 0.01 and 0.0 < eb < 0.05 and m1 > 0.0):
        raise ValueError("DELTET periodic parameters outside plausible range")

    kernel_name = src.replace("\\", "/").split("/")[-1]
    asset = {
        "description": "Stellar Navigator SN5 offline leap-second / DELTET asset (derived, not authoritative)",
        # Deterministic by design: no wall-clock timestamps inside the asset,
        # so regenerating from the same kernel yields identical bytes.
        "retrievalDate": "2026-10-12",
        "kernel": {
            "filename": kernel_name,
            "sourceUrl": "https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/" + kernel_name,
            "sha256": kernel_sha256,
        },
        "lastAnnouncedLeapSecondUtc": "2016-12-31T23:59:60Z",
        "lastTransitionUtc": transitions[-1]["utc"],
        "deltet": {
            "deltaTaSec": delta_t_a,
            "k": k,
            "eb": eb,
            "m0Rad": m0,
            "m1RadPerSec": m1,
        },
        "schema": SCHEMA,
        "transitions": transitions,
    }
    blob = (json.dumps(asset, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")
    with open(output, "wb") as f:
        f.write(blob)
    asset_sha = hashlib.sha256(blob).hexdigest()

    print(f"kernel : {kernel_name}")
    print(f"  sha256: {kernel_sha256}")
    print(f"transitions: {len(transitions)} "
          f"({transitions[0]['utc']} off={transitions[0]['offset']} .. "
          f"{transitions[-1]['utc']} off={transitions[-1]['offset']})")
    print(f"DELTET : TA={delta_t_a} K={k} EB={eb} M=({m0},{m1})")
    print(f"asset  : {output} ({len(blob)} bytes)")
    print(f"  sha256: {asset_sha}")
    print("Embed this asset SHA-256 as the trusted digest in LeapSecondProvider.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
