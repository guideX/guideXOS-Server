#!/usr/bin/env python3
"""Preallocate and verify only the DM18 raw-image target extent."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


SECTOR_BYTES = 512
TARGET_LBA = 0x12BFF7
TARGET_SECTORS = 9
GUARD_SECTORS = 1


def write_all(stream, data: bytes) -> None:
    view = memoryview(data)
    written = 0
    while written < len(view):
        count = stream.write(view[written:])
        if not count:
            raise OSError("short write while preparing DM18 target extent")
        written += count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", required=True, type=Path)
    args = parser.parse_args()
    path = args.path.resolve(strict=True)
    target_offset = TARGET_LBA * SECTOR_BYTES
    guard_offset = target_offset - GUARD_SECTORS * SECTOR_BYTES
    byte_count = (TARGET_SECTORS + GUARD_SECTORS) * SECTOR_BYTES
    if path.stat().st_size < target_offset + TARGET_SECTORS * SECTOR_BYTES:
        raise ValueError("raw image is too small for the DM18 target range")

    with path.open("r+b", buffering=0) as stream:
        stream.seek(guard_offset)
        write_all(stream, bytes([0xA5]) * byte_count)
        stream.flush()
        os.fsync(stream.fileno())
        stream.seek(guard_offset)
        write_all(stream, bytes(byte_count))
        stream.flush()
        os.fsync(stream.fileno())
        stream.seek(guard_offset)
        actual = stream.read(byte_count)
    if len(actual) != byte_count or any(actual):
        raise ValueError(
            "preallocated target and guard sector did not read back as zero; "
            "refusing to boot this fixture"
        )

    print(
        json.dumps(
            {
                "path": str(path),
                "targetLba": f"0x{TARGET_LBA:X}",
                "targetSectors": TARGET_SECTORS,
                "guardLba": f"0x{TARGET_LBA - 1:X}",
                "targetAndGuardBytes": byte_count,
                "targetAndGuardAllZero": True,
                "targetAndGuardSha256": hashlib.sha256(actual).hexdigest().upper(),
                "writer": "Python binary file stream with flush and fsync",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
