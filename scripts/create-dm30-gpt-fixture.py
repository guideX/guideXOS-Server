#!/usr/bin/env python3
"""Create a disposable, three-partition GPT image for DM30 repair proofs."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import struct
import uuid
import zlib


SECTOR_LAYOUTS = {
    512: {
        "size_bytes": 600 * 1024 * 1024,
        "partitions": ((2048, 131071), (133120, 262143), (264192, 393215)),
    },
    4096: {
        "size_bytes": 640 * 1024 * 1024,
        "partitions": ((256, 16639), (16896, 33279), (33536, 49919)),
    },
}
PARTITION_TYPE = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7")
DISK_GUID = uuid.UUID("a209d030-6e35-41f0-a030-d15c05f13030")
PARTITION_GUIDS = (
    uuid.UUID("d0300001-0000-4000-8000-000000000001"),
    uuid.UUID("d0300002-0000-4000-8000-000000000002"),
    uuid.UUID("d0300003-0000-4000-8000-000000000003"),
)
NAMES = ("DM30 Partition A", "DM30 Partition B", "DM30 Partition C")
ATTRIBUTES = (0x0000000000000001, 0x0000000000000005,
              0x1000000000000002)


def header(sector_size: int, current: int, backup: int, first_usable: int,
           last_usable: int, array_lba: int, array_crc: int) -> bytes:
    raw = bytearray(sector_size)
    raw[:8] = b"EFI PART"
    struct.pack_into("<I", raw, 8, 0x00010000)
    struct.pack_into("<I", raw, 12, 92)
    struct.pack_into("<Q", raw, 24, current)
    struct.pack_into("<Q", raw, 32, backup)
    struct.pack_into("<Q", raw, 40, first_usable)
    struct.pack_into("<Q", raw, 48, last_usable)
    raw[56:72] = DISK_GUID.bytes_le
    struct.pack_into("<Q", raw, 72, array_lba)
    struct.pack_into("<I", raw, 80, 128)
    struct.pack_into("<I", raw, 84, 128)
    struct.pack_into("<I", raw, 88, array_crc)
    struct.pack_into("<I", raw, 16, zlib.crc32(raw[:92]) & 0xFFFFFFFF)
    return bytes(raw)


def canary_bytes(sector_size: int, partition_index: int,
                 position_index: int) -> bytes:
    seed = f"DM30-CANARY-{sector_size}-{partition_index}-{position_index}".encode()
    digest = hashlib.sha256(seed).digest()
    data = bytearray(sector_size)
    for offset in range(0, sector_size, len(digest)):
        data[offset:offset + len(digest)] = digest[:min(len(digest), sector_size - offset)]
    data[:len(seed)] = seed[:sector_size]
    return bytes(data)


def mark_sparse(image) -> None:
    if os.name != "nt":
        return
    import msvcrt
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    device_io_control = kernel32.DeviceIoControl
    device_io_control.argtypes = (
        wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
        wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID)
    device_io_control.restype = wintypes.BOOL
    returned = wintypes.DWORD()
    handle = wintypes.HANDLE(msvcrt.get_osfhandle(image.fileno()))
    if not device_io_control(handle, 0x000900C4, None, 0, None, 0,
                             ctypes.byref(returned), None):
        raise OSError(ctypes.get_last_error(), "FSCTL_SET_SPARSE failed")


def make_entry(index: int, first: int, last: int) -> bytes:
    entry = bytearray(128)
    entry[:16] = PARTITION_TYPE.bytes_le
    entry[16:32] = PARTITION_GUIDS[index].bytes_le
    struct.pack_into("<Q", entry, 32, first)
    struct.pack_into("<Q", entry, 40, last)
    struct.pack_into("<Q", entry, 48, ATTRIBUTES[index])
    encoded_name = NAMES[index].encode("utf-16le")
    entry[56:56 + len(encoded_name)] = encoded_name
    return bytes(entry)


def create(path: str, sector_size: int) -> dict:
    layout = SECTOR_LAYOUTS[sector_size]
    size_bytes = layout["size_bytes"]
    total_sectors = size_bytes // sector_size
    array = bytearray(128 * 128)
    partitions = []
    for index, (first, last) in enumerate(layout["partitions"]):
        entry = make_entry(index, first, last)
        array[index * 128:(index + 1) * 128] = entry
        canaries = []
        locations = (first, first + (last - first) // 2, last)
        for position_index, lba in enumerate(locations):
            value = canary_bytes(sector_size, index, position_index)
            canaries.append({
                "lba": lba,
                "sha256": hashlib.sha256(value).hexdigest(),
            })
        partitions.append({
            "index": index + 1,
            "type_guid": str(PARTITION_TYPE),
            "unique_guid": str(PARTITION_GUIDS[index]),
            "start_lba": first,
            "end_lba": last,
            "attributes": ATTRIBUTES[index],
            "name": NAMES[index],
            "filesystem": None,
            "canaries": canaries,
        })

    array_crc = zlib.crc32(array) & 0xFFFFFFFF
    array_sectors = (len(array) + sector_size - 1) // sector_size
    first_usable = 2 + array_sectors
    backup_header_lba = total_sectors - 1
    backup_array_lba = backup_header_lba - array_sectors
    last_usable = backup_array_lba - 1

    if os.path.exists(path):
        raise FileExistsError(f"Refusing to overwrite existing fixture: {path}")
    with open(path, "xb", buffering=0) as image:
        mark_sparse(image)
        image.truncate(size_bytes)
        mbr = bytearray(sector_size)
        mbr[440:444] = b"DM30"
        mbr[446 + 4] = 0xEE
        struct.pack_into("<I", mbr, 446 + 8, 1)
        struct.pack_into("<I", mbr, 446 + 12, min(total_sectors - 1, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        image.seek(0)
        image.write(mbr)
        image.seek(sector_size)
        image.write(header(sector_size, 1, backup_header_lba, first_usable,
                           last_usable, 2, array_crc))
        image.seek(2 * sector_size)
        image.write(array)
        image.seek(backup_array_lba * sector_size)
        image.write(array)
        image.seek(backup_header_lba * sector_size)
        image.write(header(sector_size, backup_header_lba, 1, first_usable,
                           last_usable, backup_array_lba, array_crc))

    # Canary sectors are written after the sparse file was created.
    for index, (first, last) in enumerate(layout["partitions"]):
        for position_index, lba in enumerate(
                (first, first + (last - first) // 2, last)):
            with open(path, "r+b", buffering=0) as image:
                image.seek(lba * sector_size)
                value = canary_bytes(sector_size, index, position_index)
                image.write(value)
            partitions[index]["canaries"][position_index]["sha256"] = \
                hashlib.sha256(value).hexdigest()

    return {
        "image": os.path.abspath(path),
        "image_bytes": size_bytes,
        "logical_sector_size": sector_size,
        "total_logical_sectors": total_sectors,
        "gpt_entry_count": 128,
        "gpt_entry_size": 128,
        "gpt_entry_array_sectors": array_sectors,
        "pmbr_sha256": hashlib.sha256(mbr).hexdigest(),
        "partitions": partitions,
        "gaps": [
            [last + 1, layout["partitions"][index + 1][0] - 1]
            for index, (_, last) in enumerate(layout["partitions"][:-1])
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--sector-size", type=int, choices=(512, 4096), required=True)
    args = parser.parse_args()
    print(json.dumps(create(args.image, args.sector_size), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
