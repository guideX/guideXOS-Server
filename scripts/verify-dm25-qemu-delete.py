#!/usr/bin/env python3
"""Independently verify the AHCI DM25 deletion image against its pre-delete copy."""

import argparse
import os
import struct
import sys
import zlib

SECTOR_SIZE = 512


def read_at(stream, offset, length):
    stream.seek(offset)
    data = stream.read(length)
    if len(data) != length:
        raise ValueError(f"short read at byte {offset}: wanted {length}, got {len(data)}")
    return data


def parse_copy(stream, header_lba, total_sectors, primary):
    header = read_at(stream, header_lba * SECTOR_SIZE, SECTOR_SIZE)
    if header[:8] != b"EFI PART":
        raise ValueError(f"missing GPT signature at LBA {header_lba}")
    header_size = struct.unpack_from("<I", header, 12)[0]
    expected_header_crc = struct.unpack_from("<I", header, 16)[0]
    if header_size < 92 or header_size > SECTOR_SIZE:
        raise ValueError(f"invalid GPT header size {header_size} at LBA {header_lba}")
    header_for_crc = bytearray(header[:header_size])
    header_for_crc[16:20] = b"\0" * 4
    actual_header_crc = zlib.crc32(header_for_crc) & 0xFFFFFFFF
    if actual_header_crc != expected_header_crc:
        raise ValueError(f"GPT header CRC mismatch at LBA {header_lba}")

    current_lba, alternate_lba, first_usable, last_usable = struct.unpack_from(
        "<QQQQ", header, 24
    )
    expected_current = 1 if primary else total_sectors - 1
    expected_alternate = total_sectors - 1 if primary else 1
    if current_lba != expected_current or alternate_lba != expected_alternate:
        raise ValueError(f"GPT header LBA pairing is invalid at LBA {header_lba}")
    array_lba = struct.unpack_from("<Q", header, 72)[0]
    entry_count, entry_size, expected_array_crc = struct.unpack_from("<III", header, 80)
    array_bytes = entry_count * entry_size
    if entry_count == 0 or entry_size < 128 or entry_bytes_too_large(array_bytes):
        raise ValueError("invalid GPT entry-array geometry")
    if array_bytes > (total_sectors * SECTOR_SIZE):
        raise ValueError("GPT entry array exceeds the disk")
    array = read_at(stream, array_lba * SECTOR_SIZE, array_bytes)
    actual_array_crc = zlib.crc32(array) & 0xFFFFFFFF
    if actual_array_crc != expected_array_crc:
        raise ValueError(f"GPT entry-array CRC mismatch at LBA {array_lba}")
    return {
        "header": header,
        "header_lba": header_lba,
        "header_size": header_size,
        "array_lba": array_lba,
        "array": array,
        "entry_count": entry_count,
        "entry_size": entry_size,
        "array_crc": expected_array_crc,
        "disk_guid": header[56:72],
        "first_usable": first_usable,
        "last_usable": last_usable,
    }


def entry_bytes_too_large(size):
    return size <= 0 or size > 16 * 1024 * 1024


def active_entries(copy):
    active = []
    size = copy["entry_size"]
    array = copy["array"]
    for slot in range(copy["entry_count"]):
        start = slot * size
        entry = array[start : start + size]
        if entry[:16] != b"\0" * 16:
            active.append((slot, entry))
    return active


def equal_region(left, right, start, length, chunk_size=1024 * 1024):
    offset = 0
    while offset < length:
        amount = min(chunk_size, length - offset)
        if read_at(left, start + offset, amount) != read_at(right, start + offset, amount):
            return False, start + offset
        offset += amount
    return True, None


def compare_outside_metadata(left, right, total_bytes, allowed_sectors):
    intervals = sorted((lba * SECTOR_SIZE, (lba + count) * SECTOR_SIZE)
                       for lba, count in allowed_sectors)
    cursor = 0
    for start, end in intervals:
        if start < cursor or end > total_bytes:
            raise ValueError("invalid or overlapping metadata write range")
        same, mismatch = equal_region(left, right, cursor, start - cursor)
        if not same:
            return False, mismatch
        cursor = end
    return equal_region(left, right, cursor, total_bytes - cursor)


def verify(before_path, after_path):
    before_size = os.path.getsize(before_path)
    after_size = os.path.getsize(after_path)
    if before_size != after_size or before_size % SECTOR_SIZE:
        raise ValueError("before and after images must have the same 512-byte-aligned size")
    total_sectors = before_size // SECTOR_SIZE
    if total_sectors < 4096:
        raise ValueError("proof image is unexpectedly small")

    with open(before_path, "rb") as before, open(after_path, "rb") as after:
        old_mbr = read_at(before, 0, SECTOR_SIZE)
        new_mbr = read_at(after, 0, SECTOR_SIZE)
        if old_mbr != new_mbr or old_mbr[510:512] != b"\x55\xaa":
            raise ValueError("protective MBR changed or has a bad signature")

        old_primary = parse_copy(before, 1, total_sectors, True)
        old_backup = parse_copy(before, total_sectors - 1, total_sectors, False)
        new_primary = parse_copy(after, 1, total_sectors, True)
        new_backup = parse_copy(after, total_sectors - 1, total_sectors, False)
        for primary, backup, label in (
            (old_primary, old_backup, "pre-delete"),
            (new_primary, new_backup, "post-delete"),
        ):
            if (primary["array"] != backup["array"] or
                    primary["entry_count"] != backup["entry_count"] or
                    primary["entry_size"] != backup["entry_size"] or
                    primary["disk_guid"] != backup["disk_guid"] or
                    primary["first_usable"] != backup["first_usable"] or
                    primary["last_usable"] != backup["last_usable"]):
                raise ValueError(f"{label} GPT copies disagree")

        old_active = active_entries(old_primary)
        new_active = active_entries(new_primary)
        if len(old_active) != 1 or len(new_active) != 0:
            raise ValueError(f"expected one pre-delete and zero post-delete partitions; got {len(old_active)} and {len(new_active)}")
        slot, deleted_entry = old_active[0]
        slot_offset = slot * old_primary["entry_size"]
        expected_array = bytearray(old_primary["array"])
        expected_array[slot_offset : slot_offset + old_primary["entry_size"]] = b"\0" * old_primary["entry_size"]
        if new_primary["array"] != bytes(expected_array):
            raise ValueError("GPT deletion changed bytes outside the selected partition entry")

        for old, new, label in (
            (old_primary, new_primary, "primary"),
            (old_backup, new_backup, "backup"),
        ):
            old_header = old["header"]
            new_header = new["header"]
            for offset in range(SECTOR_SIZE):
                if 16 <= offset < 20 or 88 <= offset < 92:
                    continue
                if old_header[offset] != new_header[offset]:
                    raise ValueError(f"{label} GPT header changed outside CRC fields")

        deleted_start, deleted_end = struct.unpack_from("<QQ", deleted_entry, 32)
        if deleted_end < deleted_start or deleted_end >= total_sectors:
            raise ValueError("pre-delete partition extent is invalid")
        data_start = deleted_start * SECTOR_SIZE
        data_length = (deleted_end - deleted_start + 1) * SECTOR_SIZE
        same, mismatch = equal_region(before, after, data_start, data_length)
        if not same:
            raise ValueError(f"deleted partition data changed at byte {mismatch}")

        entry_size = old_primary["entry_size"]
        first_entry_sector = slot_offset // SECTOR_SIZE
        last_entry_sector = (slot_offset + entry_size - 1) // SECTOR_SIZE
        touched = last_entry_sector - first_entry_sector + 1
        allowed = [
            (1, 1),
            (total_sectors - 1, 1),
            (old_primary["array_lba"] + first_entry_sector, touched),
            (old_backup["array_lba"] + first_entry_sector, touched),
        ]
        same, mismatch = compare_outside_metadata(before, after, before_size, allowed)
        if not same:
            raise ValueError(f"image changed outside GPT metadata at byte {mismatch}")

    print("result=PASS")
    print(f"logicalSectorSize={SECTOR_SIZE}")
    print(f"deletedPartitionSlot={slot + 1}")
    print(f"deletedStartLba={deleted_start}")
    print(f"deletedEndLba={deleted_end}")
    print(f"deletedPartitionBytes={data_length}")
    print("primaryGptCrc=PASS")
    print("backupGptCrc=PASS")
    print("unrelatedEntryBytes=UNCHANGED")
    print("deletedPartitionData=BYTE-FOR-BYTE-UNCHANGED")
    print("allNonMetadataImageBytes=BYTE-FOR-BYTE-UNCHANGED")
    print("protectiveMbr=UNCHANGED")
    print("metadataWriteSectors=" + ",".join(
        str(lba) if count == 1 else f"{lba}+{count}"
        for lba, count in allowed
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", help="raw image copied after lifecycle and before deletion")
    parser.add_argument("after", help="raw image after deletion and restart proof")
    args = parser.parse_args()
    try:
        verify(args.before, args.after)
    except (OSError, ValueError, struct.error) as error:
        print(f"result=FAIL detail={error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
