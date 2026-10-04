#!/usr/bin/env python3
"""Corrupt one byte in a disposable 512-byte-sector GPT entry array."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="disposable raw image; modified in place")
    parser.add_argument("--side", choices=("Primary", "Backup"),
                        default="Backup", help="GPT side whose array to damage")
    args = parser.parse_args()
    size = os.path.getsize(args.image)
    sector_size = 512
    if size == 0 or size % sector_size:
        raise SystemExit("image size is not a multiple of 512 bytes")
    total_sectors = size // sector_size
    with open(args.image, "r+b", buffering=0) as image:
        header_lba = 1 if args.side == "Primary" else total_sectors - 1
        image.seek(header_lba * sector_size)
        header = image.read(sector_size)
        if len(header) != sector_size or header[:8] != b"EFI PART":
            raise SystemExit("backup GPT header signature is missing")
        if u64(header, 24) != header_lba:
            raise SystemExit(f"{args.side.lower()} GPT header is not at expected LBA {header_lba}")
        array_lba = u64(header, 72)
        entry_count = u32(header, 80)
        entry_size = u32(header, 84)
        if entry_count == 0 or entry_size < 128 or entry_size % 8:
            raise SystemExit("unsupported partition-entry geometry")
        array_bytes = entry_count * entry_size
        array_offset = array_lba * sector_size
        if (array_lba <= 1 or array_offset + array_bytes > size or
                (args.side == "Backup" and array_offset + array_bytes > header_lba * sector_size)):
            raise SystemExit(f"{args.side.lower()} entry array is outside expected metadata bounds")
        image.seek(array_offset)
        array = image.read(array_bytes)
        if len(array) != array_bytes:
            raise SystemExit("short read from backup entry array")
        active = next((index for index in range(entry_count)
                       if array[index * entry_size:index * entry_size + 16] != bytes(16)), None)
        if active is None:
            raise SystemExit("no active partition entry to corrupt")
        offset = array_offset + active * entry_size + 56
        image.seek(offset)
        original = image.read(1)
        if len(original) != 1:
            raise SystemExit("failed to read active partition-name byte")
        image.seek(offset)
        image.write(bytes([original[0] ^ 0x01]))
        image.flush()
        os.fsync(image.fileno())
    print(json.dumps({
        "image": os.path.abspath(args.image),
        "damaged_side": args.side,
        "sector_size": sector_size,
        "header_lba": header_lba,
        "array_lba": array_lba,
        "entry_count": entry_count,
        "entry_size": entry_size,
        "corrupted_entry_number": active + 1,
        "corrupted_byte_offset": offset,
        "corrupted_byte_lba": offset // sector_size,
        "corruption": "one active-entry name byte; selected array CRC now invalid",
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
