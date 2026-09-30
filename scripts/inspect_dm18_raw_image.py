#!/usr/bin/env python3
"""Independently inspect the DM18 target and hash a raw backing image."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


SECTOR_BYTES = 512
TARGET_LBA = 0x12BFF7
TARGET_SECTORS = 9


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    args = parser.parse_args()

    path = args.path.resolve(strict=True)
    output_dir = args.output_dir.resolve(strict=True)
    logical_bytes = path.stat().st_size
    target_offset = TARGET_LBA * SECTOR_BYTES
    target_bytes = TARGET_SECTORS * SECTOR_BYTES
    if logical_bytes < target_offset + target_bytes:
        raise ValueError("DM18 target region extends beyond the raw image")

    with path.open("rb", buffering=0) as stream:
        stream.seek(target_offset - SECTOR_BYTES)
        guard = stream.read(SECTOR_BYTES)
        target = stream.read(target_bytes)
        if len(guard) != SECTOR_BYTES or len(target) != target_bytes:
            raise OSError("short read while inspecting DM18 target range")
        stream.seek(0)
        logical_hash = hashlib.sha256()
        while chunk := stream.read(1024 * 1024):
            logical_hash.update(chunk)

    pattern = bytes((index * 37 + 0x5A) & 0xFF for index in range(target_bytes))
    result = {
        "phase": args.phase,
        "path": str(path),
        "logicalBytes": logical_bytes,
        "targetLba": f"0x{TARGET_LBA:X}",
        "targetSectors": TARGET_SECTORS,
        "targetBytes": target_bytes,
        "targetSha256": hashlib.sha256(target).hexdigest().upper(),
        "targetAllZero": not any(target),
        "guardLba": f"0x{TARGET_LBA - 1:X}",
        "guardSha256": hashlib.sha256(guard).hexdigest().upper(),
        "guardAllZero": not any(guard),
        "cycle1PatternSha256": hashlib.sha256(pattern).hexdigest().upper(),
        "logicalImageSha256": logical_hash.hexdigest().upper(),
    }
    stem = f"host-target-{args.phase}"
    (output_dir / f"{stem}.bin").write_bytes(target)
    (output_dir / f"{stem}.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    line = (
        f"phase={args.phase} lba={result['targetLba']} "
        f"sectors={TARGET_SECTORS} bytes={target_bytes} "
        f"actualSha256={result['targetSha256']} "
        f"cycle1PatternSha256={result['cycle1PatternSha256']} "
        f"allZero={str(result['targetAllZero']).lower()} "
        f"precedingSectorAllZero={str(result['guardAllZero']).lower()}"
    )
    with (output_dir / "host-target-inspection.txt").open(
        "a", encoding="ascii"
    ) as stream:
        stream.write(line + "\n")
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
