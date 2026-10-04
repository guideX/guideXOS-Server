#!/usr/bin/env python3
"""Independently inspect GPT metadata and compare before/after repair images."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import uuid
import zlib


SIGNATURE = b"EFI PART"
MAX_ENTRY_COUNT = 1_000_000
MAX_ENTRY_SIZE = 4096
MAX_ARRAY_BYTES = 64 * 1024 * 1024


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def guid(data: bytes) -> str:
    return str(uuid.UUID(bytes_le=data))


def read_exact(handle, offset: int, size: int) -> bytes:
    handle.seek(offset)
    data = handle.read(size)
    if len(data) != size:
        raise ValueError(f"short read at byte offset {offset}")
    return data


def parse_entries(array: bytes, count: int, size: int) -> list[dict]:
    result = []
    for index in range(count):
        entry = array[index * size : (index + 1) * size]
        type_guid = entry[:16]
        if type_guid == bytes(16):
            continue
        first = u64(entry, 32)
        last = u64(entry, 40)
        result.append({
            "number": index + 1,
            "type_guid": guid(type_guid),
            "unique_guid": guid(entry[16:32]),
            "first_lba": first,
            "last_lba": last,
            "sectors": last - first + 1 if last >= first else 0,
            "attributes": u64(entry, 48),
            "name": entry[56:128].decode("utf-16le", errors="replace").split("\0", 1)[0],
        })
    return result


def parse_copy(handle, sector_size: int, total_sectors: int, lba: int,
               primary: bool) -> tuple[dict, bytes | None, bytes | None]:
    result = {
        "state": "HeaderInvalid",
        "header_lba": lba,
        "header_crc_valid": False,
        "array_crc_valid": False,
        "stored_header_crc": None,
        "calculated_header_crc": None,
        "stored_array_crc": None,
        "calculated_array_crc": None,
        "disk_guid": None,
        "current_lba": None,
        "backup_lba": None,
        "entry_array_lba": None,
        "entry_count": None,
        "entry_size": None,
    }
    header = read_exact(handle, lba * sector_size, sector_size)
    if header[:8] != SIGNATURE:
        result["state"] = "NotPresent"
        return result, None, None

    revision = u32(header, 8)
    header_size = u32(header, 12)
    result["revision"] = revision
    result["header_size"] = header_size
    if revision != 0x00010000 or header_size < 92 or header_size > sector_size:
        result["state"] = "Unsupported"
        return result, header, None

    stored_header_crc = u32(header, 16)
    header_crc_data = bytearray(header[:header_size])
    header_crc_data[16:20] = bytes(4)
    calculated_header_crc = zlib.crc32(header_crc_data) & 0xFFFFFFFF
    result["stored_header_crc"] = f"0x{stored_header_crc:08x}"
    result["calculated_header_crc"] = f"0x{calculated_header_crc:08x}"
    result["header_crc_valid"] = stored_header_crc == calculated_header_crc

    current_lba = u64(header, 24)
    backup_lba = u64(header, 32)
    first_usable = u64(header, 40)
    last_usable = u64(header, 48)
    disk_guid = guid(header[56:72])
    array_lba = u64(header, 72)
    entry_count = u32(header, 80)
    entry_size = u32(header, 84)
    stored_array_crc = u32(header, 88)
    result.update({
        "current_lba": current_lba,
        "backup_lba": backup_lba,
        "first_usable_lba": first_usable,
        "last_usable_lba": last_usable,
        "disk_guid": disk_guid,
        "entry_array_lba": array_lba,
        "entry_count": entry_count,
        "entry_size": entry_size,
        "stored_array_crc": f"0x{stored_array_crc:08x}",
    })

    if (not entry_count or entry_count > MAX_ENTRY_COUNT or entry_size < 128 or
            entry_size > MAX_ENTRY_SIZE or entry_size % 8):
        result["state"] = "Unsupported"
        return result, header, None
    array_bytes = entry_count * entry_size
    if array_bytes > MAX_ARRAY_BYTES:
        result["state"] = "Unsupported"
        return result, header, None
    array_sectors = (array_bytes + sector_size - 1) // sector_size
    if (array_lba >= total_sectors or array_sectors > total_sectors - array_lba or
            current_lba != lba or backup_lba >= total_sectors or
            first_usable > last_usable or last_usable >= total_sectors):
        result["state"] = "BoundsInvalid"
        return result, header, None
    if primary:
        expected_location = current_lba == 1 and backup_lba == total_sectors - 1
        allowed_array = array_lba >= 2 and array_lba + array_sectors <= first_usable
    else:
        expected_location = current_lba == total_sectors - 1 and backup_lba == 1
        allowed_array = array_lba + array_sectors == current_lba and array_lba > last_usable
    if not expected_location or not allowed_array:
        result["state"] = "GeometryMismatch"
        return result, header, None

    array = read_exact(handle, array_lba * sector_size, array_bytes)
    calculated_array_crc = zlib.crc32(array) & 0xFFFFFFFF
    result["calculated_array_crc"] = f"0x{calculated_array_crc:08x}"
    result["array_crc_valid"] = stored_array_crc == calculated_array_crc
    result["state"] = (
        "Valid" if result["header_crc_valid"] and result["array_crc_valid"]
        else "HeaderCrcInvalid" if not result["header_crc_valid"]
        else "ArrayCrcInvalid"
    )
    result["entry_array_sha256"] = hashlib.sha256(array).hexdigest()
    result["partitions"] = parse_entries(array, entry_count, entry_size)
    return result, header, array


def inspect(path: str, sector_size: int) -> dict:
    size = os.path.getsize(path)
    if not size or size % sector_size:
        raise ValueError(f"{path}: image size is not a multiple of {sector_size}")
    total_sectors = size // sector_size
    with open(path, "rb") as image:
        mbr = read_exact(image, 0, sector_size)
        mbr_signature = mbr[510:512] == b"\x55\xaa"
        mbr_entries = []
        for slot in range(4):
            entry = mbr[446 + slot * 16 : 462 + slot * 16]
            if entry[4] != 0 or u32(entry, 12) != 0:
                mbr_entries.append({
                    "type": entry[4],
                    "start_lba": u32(entry, 8),
                    "sectors": u32(entry, 12),
                })
        protective = any(entry["type"] == 0xEE for entry in mbr_entries)
        protective_entries = [entry for entry in mbr_entries
                              if entry["type"] == 0xEE]
        protective_valid = (
            mbr_signature and len(mbr_entries) == 1 and
            len(protective_entries) == 1 and
            protective_entries[0]["start_lba"] == 1 and
            protective_entries[0]["sectors"] == min(total_sectors - 1, 0xFFFFFFFF)
        )
        primary, _, primary_array = parse_copy(image, sector_size, total_sectors, 1, True)
        backup, _, backup_array = parse_copy(
            image, sector_size, total_sectors, total_sectors - 1, False)

    primary_valid = primary["state"] == "Valid"
    backup_valid = backup["state"] == "Valid"
    normalized_equal = False
    if primary_valid and backup_valid:
        normalized_keys = ("disk_guid", "first_usable_lba", "last_usable_lba",
                           "entry_count", "entry_size")
        normalized_equal = (
            all(primary[key] == backup[key] for key in normalized_keys) and
            primary_array == backup_array
        )
    if primary_valid and backup_valid:
        redundancy = "Healthy" if normalized_equal else "Conflict"
    elif primary_valid or backup_valid:
        redundancy = "Degraded"
    else:
        redundancy = "Unrecoverable"
    authoritative = "Primary" if primary_valid and not backup_valid else (
        "Backup" if backup_valid and not primary_valid else None)
    partitions = (primary.get("partitions", []) if primary_valid and
                  (not backup_valid or normalized_equal) else
                  backup.get("partitions", []) if backup_valid and
                  (not primary_valid or normalized_equal) else [])
    return {
        "path": os.path.abspath(path),
        "logical_sector_size": sector_size,
        "image_bytes": size,
        "total_logical_sectors": total_sectors,
        "protective_mbr": {
            "signature_valid": mbr_signature,
            "protective_entry_present": protective,
            "entry_count": len(mbr_entries),
            "valid": protective_valid,
            "sha256": hashlib.sha256(mbr).hexdigest(),
        },
        "primary": primary,
        "backup": backup,
        "normalized_copies_equal": normalized_equal,
        "redundancy": redundancy,
        "authoritative_copy": authoritative,
        "disk_guid": primary.get("disk_guid") if primary_valid else
                     backup.get("disk_guid") if backup_valid else None,
        "partitions": partitions,
        "metadata_ranges": metadata_ranges(sector_size, total_sectors,
                                            primary, backup),
    }


def metadata_ranges(sector_size: int, total_sectors: int,
                    primary: dict, backup: dict) -> list[tuple[int, int, str]]:
    ranges = [(1, 1, "primary-header"),
              (total_sectors - 1, total_sectors - 1, "backup-header")]
    for copy in (primary, backup):
        start = copy.get("entry_array_lba")
        count = copy.get("entry_count")
        width = copy.get("entry_size")
        if (isinstance(start, int) and isinstance(count, int) and
                isinstance(width, int) and count > 0 and width > 0):
            sectors = (count * width + sector_size - 1) // sector_size
            if start < total_sectors and sectors <= total_sectors - start:
                ranges.append((start, start + sectors - 1, "entry-array"))
    return ranges


def changed_ranges(before_path: str, after_path: str, sector_size: int,
                   allowed: list[tuple[int, int, str]]) -> dict:
    before_size = os.path.getsize(before_path)
    after_size = os.path.getsize(after_path)
    if before_size != after_size or before_size % sector_size:
        return {"same_image_size": False, "ranges": [], "metadata_only": False}
    total_sectors = before_size // sector_size
    changed = []
    sectors_per_chunk = max(1, 1024 * 1024 // sector_size)
    with open(before_path, "rb") as before, open(after_path, "rb") as after:
        active_start = None
        active_end = None
        for first_lba in range(0, total_sectors, sectors_per_chunk):
            count = min(sectors_per_chunk, total_sectors - first_lba)
            byte_count = count * sector_size
            offset = first_lba * sector_size
            old = read_exact(before, offset, byte_count)
            new = read_exact(after, offset, byte_count)
            for local_lba in range(count):
                local_offset = local_lba * sector_size
                different = old[local_offset:local_offset + sector_size] != \
                            new[local_offset:local_offset + sector_size]
                lba = first_lba + local_lba
                if different:
                    if active_start is None:
                        active_start = lba
                    active_end = lba
                elif active_start is not None:
                    changed.append((active_start, active_end))
                    active_start = active_end = None
        if active_start is not None:
            changed.append((active_start, active_end))
    ranges = []
    metadata_only = True
    for start, end in changed:
        labels = sorted({label for first, last, label in allowed
                         if start <= last and first <= end})
        if any(not any(first <= lba <= last for first, last, _ in allowed)
               for lba in range(start, end + 1)):
            metadata_only = False
        ranges.append({"first_lba": start, "last_lba": end,
                       "classification": labels or ["outside-gpt-metadata"]})
    return {"same_image_size": True, "ranges": ranges,
            "metadata_only": metadata_only}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="image to inspect (normally the after image)")
    parser.add_argument("--sector-size", type=int, choices=(512, 4096), required=True)
    parser.add_argument("--before", help="optional pre-repair image for byte-level diff")
    args = parser.parse_args()
    try:
        report = inspect(args.image, args.sector_size)
        if args.before:
            before = inspect(args.before, args.sector_size)
            report["before"] = {
                "path": before["path"],
                "protective_mbr_sha256": before["protective_mbr"]["sha256"],
                "primary_state": before["primary"]["state"],
                "backup_state": before["backup"]["state"],
                "redundancy": before["redundancy"],
            }
            report["changed_ranges"] = changed_ranges(
                args.before, args.image, args.sector_size,
                before["metadata_ranges"] + report["metadata_ranges"])
            report["protective_mbr_unchanged"] = (
                before["protective_mbr"]["sha256"] ==
                report["protective_mbr"]["sha256"])
        print(json.dumps(report, indent=2, sort_keys=True))
        if report["redundancy"] != "Healthy":
            return 1
        if args.before and (not report["changed_ranges"]["same_image_size"] or
                            not report["changed_ranges"]["metadata_only"] or
                            not report["protective_mbr_unchanged"]):
            return 1
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(f"verification failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
