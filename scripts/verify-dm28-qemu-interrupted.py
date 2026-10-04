#!/usr/bin/env python3
"""Inspect a real QEMU image stopped at DM28's post-invalidation marker."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

SECTOR_SIZE = 4096
GPT_BASIC_DATA = bytes.fromhex("A2A0D0EBE5B9334487C068B6B72699C7")
MARKER_MAGIC = b"GXDM26RF"


class Image:
    def __init__(self, path: Path):
        self.path = path.resolve(strict=True)
        self.stream = self.path.open("rb")
        self.size = self.path.stat().st_size
        if self.size % SECTOR_SIZE:
            raise ValueError(f"{self.path.name}: image is not 4Kn aligned")
        self.sectors = self.size // SECTOR_SIZE

    def read(self, offset: int, size: int) -> bytes:
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise ValueError(f"{self.path.name}: short read at {offset}")
        return data

    def sector(self, lba: int) -> bytes:
        if not 0 <= lba < self.sectors:
            raise ValueError(f"{self.path.name}: LBA {lba} outside image")
        return self.read(lba * SECTOR_SIZE, SECTOR_SIZE)

    def close(self) -> None:
        self.stream.close()


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def gpt(image: Image) -> dict:
    if image.sector(0)[510:512] != b"\x55\xaa":
        raise ValueError("protective MBR signature is invalid")

    def read_copy(lba: int, primary: bool) -> dict:
        header = image.sector(lba)
        if header[:8] != b"EFI PART":
            raise ValueError(f"GPT header missing at LBA {lba}")
        size = u32(header, 12)
        if not 92 <= size <= SECTOR_SIZE:
            raise ValueError("GPT header size is invalid")
        check = bytearray(header[:size])
        stored_header_crc = u32(check, 16)
        check[16:20] = b"\0" * 4
        if zlib.crc32(check) & 0xFFFFFFFF != stored_header_crc:
            raise ValueError(f"GPT header CRC mismatch at LBA {lba}")
        current, backup = struct.unpack_from("<QQ", header, 24)
        expected = (1, image.sectors - 1) if primary else (image.sectors - 1, 1)
        if (current, backup) != expected:
            raise ValueError("GPT header pointers are inconsistent")
        array_lba = u64(header, 72)
        count, entry_size, array_crc = struct.unpack_from("<III", header, 80)
        if count != 128 or entry_size != 128:
            raise ValueError("GPT entry geometry is unexpected")
        array = image.read(array_lba * SECTOR_SIZE, count * entry_size)
        if zlib.crc32(array) & 0xFFFFFFFF != array_crc:
            raise ValueError("GPT partition-array CRC mismatch")
        return {"header": header, "array": array, "array_crc": array_crc,
                "disk_guid": header[56:72], "first": u64(header, 40),
                "last": u64(header, 48)}

    primary = read_copy(1, True)
    backup = read_copy(image.sectors - 1, False)
    if primary["array"] != backup["array"] or \
            primary["disk_guid"] != backup["disk_guid"]:
        raise ValueError("primary and backup GPT copies disagree")
    found = []
    for slot in range(128):
        entry = primary["array"][slot * 128:(slot + 1) * 128]
        if entry[:16] != GPT_BASIC_DATA:
            continue
        name = entry[56:128].decode("utf-16le", errors="strict").split("\0", 1)[0]
        if name == "DM9 QEMU Proof":
            found.append((slot + 1, entry, u64(entry, 32), u64(entry, 40)))
    if len(found) != 1:
        raise ValueError("expected one DM9 QEMU proof partition")
    number, entry, start, end = found[0]
    if not primary["first"] <= start <= end <= primary["last"]:
        raise ValueError("proof partition is outside GPT usable LBAs")
    return {"primary": primary, "backup": backup, "number": number,
            "entry": entry, "start": start, "end": end}


def verify(before_path: Path, interrupted_path: Path) -> list[str]:
    before = Image(before_path)
    interrupted = Image(interrupted_path)
    try:
        if before.size != interrupted.size:
            raise ValueError("checkpoint image sizes differ")
        old_table = gpt(before)
        current_table = gpt(interrupted)
        if (old_table["start"], old_table["end"], old_table["number"],
                old_table["entry"], old_table["primary"]["disk_guid"]) != \
                (current_table["start"], current_table["end"],
                 current_table["number"], current_table["entry"],
                 current_table["primary"]["disk_guid"]):
            raise ValueError("partition identity changed during interruption")

        start = old_table["start"]
        old_boot = before.sector(start)
        if old_boot[510:512] != b"\x55\xaa" or \
                old_boot[71:82] != b"DM9PROOF   ":
            raise ValueError("pre-reformat FAT32 fixture is not valid DM9PROOF")
        if u16(old_boot, 11) != SECTOR_SIZE:
            raise ValueError("pre-reformat fixture is not 4Kn")

        marker_lba = None
        marker = None
        for relative in range(8, 32):
            candidate = interrupted.sector(start + relative)
            if candidate[:8] == MARKER_MAGIC:
                marker_lba, marker = relative, candidate
                break
        if marker is None:
            raise ValueError("persistent Quick Reformat marker is missing")
        version = u32(marker, 8)
        scheme = u32(marker, 12)
        marker_start = u64(marker, 16)
        marker_count = u64(marker, 24)
        marker_sector_size = u32(marker, 32)
        partition_number = u32(marker, 36)
        backup_boot = u32(marker, 96)
        stored_lba = u32(marker, 116)
        fingerprint = u32(marker, 120)
        state = u32(marker, 124)
        checksum = u32(marker, 128)
        if version != 2 or scheme != 2 or marker_start != start or \
                marker_count != old_table["end"] - start + 1 or \
                marker_sector_size != SECTOR_SIZE or \
                partition_number != old_table["number"] or \
                stored_lba != marker_lba or state != 2:
            raise ValueError("marker structure, target, or point-of-no-return state is invalid")
        if marker[48:64] != old_table["entry"][16:32] or \
                marker[64:80] != old_table["entry"][:16] or \
                marker[80:96] != old_table["primary"]["disk_guid"]:
            raise ValueError("marker partition or disk GUID does not match GPT")
        if fingerprint != current_table["primary"]["array_crc"]:
            raise ValueError("marker GPT table fingerprint does not match the current table")
        if checksum != fnv1a(marker[:128]):
            raise ValueError("marker checksum is invalid")

        primary_boot = interrupted.sector(start)
        backup_boot_bytes = interrupted.sector(start + backup_boot)
        if primary_boot[510:512] == b"\x55\xaa" or \
                backup_boot_bytes[510:512] == b"\x55\xaa":
            raise ValueError("old primary or backup BPB still has boot authority")

        bps = u16(old_boot, 11)
        reserved = u16(old_boot, 14)
        fats = old_boot[16]
        fat_sectors = u32(old_boot, 36)
        if bps != SECTOR_SIZE or fats != 2 or not fat_sectors:
            raise ValueError("pre-reformat FAT geometry is invalid")
        fat1_lba = start + reserved
        fat2_lba = fat1_lba + fat_sectors
        fat_bytes = fat_sectors * SECTOR_SIZE
        if before.read(fat1_lba * SECTOR_SIZE, fat_bytes) != \
                interrupted.read(fat1_lba * SECTOR_SIZE, fat_bytes):
            raise ValueError("FAT1 changed before the QRF_FAT1_BEGIN interruption")
        if before.read(fat2_lba * SECTOR_SIZE, fat_bytes) != \
                interrupted.read(fat2_lba * SECTOR_SIZE, fat_bytes):
            raise ValueError("FAT2 changed before the QRF_FAT1_BEGIN interruption")

        root_cluster = u32(old_boot, 44)
        spc = old_boot[13]
        first_data = start + reserved + fats * fat_sectors
        root_lba = first_data + (root_cluster - 2) * spc
        root_bytes = spc * SECTOR_SIZE
        if before.read(root_lba * SECTOR_SIZE, root_bytes) != \
                interrupted.read(root_lba * SECTOR_SIZE, root_bytes):
            raise ValueError("root cluster changed before FAT1 initialization")

        return [
            "DM28 interrupted-image verifier: PASS",
            f"GPT disk/partition identity preserved; partition={partition_number} startLba={start} endLba={old_table['end']}",
            f"primary and backup old BPBs invalid; partition logical sector size={SECTOR_SIZE}",
            f"marker=GXDM26RF version={version} state=point-of-no-return sector={marker_lba}",
            f"marker GUIDs and GPT entry-array fingerprint=PASS crc32={fingerprint:08X}",
            f"marker checksum=PASS checksum={checksum:08X}; pre-reformat volumeId={u32(marker, 100):08X}",
            f"FAT1 and FAT2 remain byte-identical to pre-reformat fixture ({fat_bytes} bytes each); root cluster unchanged",
            "the interruption occurred after durable invalidation and before FAT1 clearing began",
        ]
    finally:
        before.close()
        interrupted.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path, help="pre-reformat 4Kn raw image")
    parser.add_argument("interrupted", type=Path, help="cold-restart source raw image")
    args = parser.parse_args()
    try:
        for line in verify(args.before, args.interrupted):
            print(line)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
