#!/usr/bin/env python3
"""Read-only, on-demand verifier for the DM22 large FAT32 QEMU image."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import uuid
import zlib
from pathlib import Path


SUPPORTED_SECTOR_SIZES = (512, 4096)
GPT_BASIC_DATA = uuid.UUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7").bytes_le
PARTITION_NAME = "DM9 QEMU Proof"
DIRECTORY_NAME = b"DM9        "
FILE_NAME = b"PROOF   BIN"
PAYLOAD_BYTES = 96 * 1024
MIN_512_VOLUME_BYTES = 8 * 1024 * 1024 * 1024
MIN_4KN_VOLUME_BYTES = 600 * 1024 * 1024
MIN_CLUSTERS = 65525
MAX_CLUSTERS = 0x0FFFFFEE
CLUSTER_MASK = 0x0FFFFFFF
EOC = 0x0FFFFFF8
ALLOCATOR_START_512 = 120001
ALLOCATOR_START_4KN = 70001


class VerificationError(RuntimeError):
    pass


def require(condition: bool, detail: str) -> None:
    if not condition:
        raise VerificationError(detail)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def expected_payload(length: int) -> bytes:
    return bytes((i * 37 + (i >> 8) * 13 + 0x5A) & 0xFF
                 for i in range(length))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="read-only raw disk image")
    parser.add_argument("--sector-size", type=int, choices=SUPPORTED_SECTOR_SIZES,
                        help="logical sector size; detected from GPT when omitted")
    args = parser.parse_args()
    image_path = args.image.resolve(strict=True)

    with image_path.open("rb") as image:
        image.seek(0, 2)
        image_bytes = image.tell()
        sector_size = args.sector_size
        if sector_size is None:
            sector_size = 0
            for candidate in SUPPORTED_SECTOR_SIZES:
                image.seek(candidate)
                if image.read(8) == b"EFI PART":
                    sector_size = candidate
                    break
        require(sector_size in SUPPORTED_SECTOR_SIZES,
                "logical sector size was not supplied or detected from GPT")
        require(image_bytes % sector_size == 0,
                f"image is not {sector_size}-byte aligned")
        total_sectors = image_bytes // sector_size

        def read_sector(lba: int) -> bytes:
            require(0 <= lba < total_sectors,
                    f"LBA {lba} is outside the image")
            image.seek(lba * sector_size)
            sector = image.read(sector_size)
            require(len(sector) == sector_size, f"short read at LBA {lba}")
            return sector

        mbr = read_sector(0)
        require(mbr[510:512] == b"\x55\xAA",
                "protective MBR signature is missing")
        protective = mbr[446:462]
        require(protective[4] == 0xEE and u32(protective, 8) == 1 and
                u32(protective, 12) == min(total_sectors - 1, 0xFFFFFFFF),
                "protective MBR does not cover the image")
        if sector_size == 4096:
            require(mbr[4094:4096] != b"\x55\xAA",
                    "protective MBR signature was incorrectly moved to the end of 4Kn LBA0")
        primary = read_sector(1)
        backup = read_sector(total_sectors - 1)
        require(primary[:8] == b"EFI PART" and backup[:8] == b"EFI PART",
                "primary or backup GPT signature is invalid")

        def validate_header(header: bytes, current_lba: int,
                            backup_lba: int) -> tuple[int, int, int, bytes, bytes]:
            header_size = u32(header, 12)
            stored_crc = u32(header, 16)
            require(92 <= header_size <= sector_size,
                    "GPT header size is invalid")
            header_copy = bytearray(header[:header_size])
            header_copy[16:20] = b"\0\0\0\0"
            require(zlib.crc32(header_copy) & 0xFFFFFFFF == stored_crc,
                    "GPT header CRC does not match")
            require(u64(header, 24) == current_lba and
                    u64(header, 32) == backup_lba,
                    "GPT current/backup LBAs are inconsistent")
            first_usable, last_usable = u64(header, 40), u64(header, 48)
            require(1 <= first_usable <= last_usable < total_sectors,
                    "GPT usable range is invalid")
            entries_lba = u64(header, 72)
            count, size, entries_crc = u32(header, 80), u32(header, 84), u32(header, 88)
            require(count == 128 and size == 128,
                    "GPT entry geometry differs from the proof format")
            image.seek(entries_lba * sector_size)
            entries = image.read(count * size)
            require(len(entries) == count * size,
                    "GPT entry array is truncated")
            require(zlib.crc32(entries) & 0xFFFFFFFF == entries_crc,
                    "GPT entry-array CRC does not match")
            return first_usable, last_usable, entries_lba, header[56:72], entries

        p_first, p_last, p_array_lba, disk_guid, p_entries = validate_header(
            primary, 1, total_sectors - 1)
        b_first, b_last, b_array_lba, backup_guid, b_entries = validate_header(
            backup, total_sectors - 1, 1)
        require((p_first, p_last, disk_guid) == (b_first, b_last, backup_guid) and
                p_entries == b_entries and p_array_lba == 2,
                "primary and backup GPT metadata disagree")
        array_sectors = (128 * 128 + sector_size - 1) // sector_size
        require(b_array_lba == total_sectors - 1 - array_sectors,
                "backup GPT entry array is misplaced")

        partitions = []
        for index in range(128):
            entry = p_entries[index * 128:(index + 1) * 128]
            if entry[:16] != GPT_BASIC_DATA:
                continue
            name = entry[56:128].decode("utf-16le", errors="strict").split("\0", 1)[0]
            partitions.append((index + 1, u64(entry, 32), u64(entry, 40), name))
        require(len(partitions) == 1, "proof disk must contain one data partition")
        partition = next((item for item in partitions if item[3] == PARTITION_NAME), None)
        require(partition is not None, "named proof partition is missing")
        partition_number, start_lba, end_lba, _ = partition
        require(p_first <= start_lba <= end_lba <= p_last,
                "proof partition escapes the GPT usable range")
        alignment_sectors = (1024 * 1024) // sector_size
        require(start_lba % alignment_sectors == 0,
                "proof partition does not preserve 1 MiB logical-sector alignment")

        boot = read_sector(start_lba)
        require(boot[510:512] == b"\x55\xAA",
                "FAT32 boot-sector signature is invalid")
        bps, spc = u16(boot, 11), boot[13]
        reserved, fats = u16(boot, 14), boot[16]
        total_sectors_fat = u32(boot, 32)
        fat_size = u32(boot, 36)
        root_cluster = u32(boot, 44)
        fsinfo_sector = u16(boot, 48)
        backup_boot_sector = u16(boot, 50)
        max_spc = min(64, 32768 // bps) if bps else 0
        require(bps == sector_size and 0 < spc <= max_spc and
                spc & (spc - 1) == 0 and spc * bps <= 32768,
                "FAT32 sector or cluster geometry is invalid")
        require(reserved > 0 and fats == 2 and fat_size > 0 and
                u16(boot, 40) == 0 and u16(boot, 42) == 0,
                "FAT32 reserved/FAT mirroring geometry is invalid")
        require(u16(boot, 17) == 0 and u16(boot, 19) == 0 and
                u16(boot, 22) == 0 and total_sectors_fat > 0,
                "FAT32 BPB uses invalid legacy fields")
        minimum_volume_bytes = (MIN_512_VOLUME_BYTES if sector_size == 512
                                else MIN_4KN_VOLUME_BYTES)
        require(total_sectors_fat >= minimum_volume_bytes // sector_size and
                total_sectors_fat <= end_lba - start_lba + 1,
                "FAT32 volume is smaller than its transport-specific proof bound or exceeds its partition")
        require(u32(boot, 28) == start_lba and
                boot[71:82] == b"DM9PROOF   " and
                boot[82:90] == b"FAT32   ",
                "FAT32 hidden-sector or volume-label fields are invalid")
        require(backup_boot_sector > 0 and backup_boot_sector < reserved and
                fsinfo_sector > 0 and fsinfo_sector < reserved,
                "FAT32 backup or FSInfo location is invalid")
        require(read_sector(start_lba + backup_boot_sector) == boot,
                "backup boot sector differs from the primary")

        primary_fsinfo = read_sector(start_lba + fsinfo_sector)
        backup_fsinfo_lba = start_lba + backup_boot_sector + fsinfo_sector
        backup_fsinfo = read_sector(backup_fsinfo_lba)

        def valid_fsinfo(data: bytes) -> bool:
            return (u32(data, 0) == 0x41615252 and
                    u32(data, 484) == 0x61417272 and
                    u32(data, 508) == 0xAA550000)

        require(valid_fsinfo(primary_fsinfo) and
                valid_fsinfo(backup_fsinfo) and
                primary_fsinfo == backup_fsinfo,
                "primary and backup FSInfo sectors disagree")
        next_free = u32(primary_fsinfo, 492)
        allocator_start = (ALLOCATOR_START_512 if sector_size == 512
                          else ALLOCATOR_START_4KN)
        require(next_free == allocator_start,
                "FSInfo does not retain the high-cluster allocator hint")

        first_data_offset = reserved + fats * fat_size
        require(first_data_offset < total_sectors_fat,
                "FAT32 metadata exceeds the volume")
        cluster_count = (total_sectors_fat - first_data_offset) // spc
        require(MIN_CLUSTERS <= cluster_count <= MAX_CLUSTERS,
                "FAT32 data-cluster count is outside the supported range")
        require((cluster_count + 2) * 4 <= fat_size * bps,
                "FAT32 tables are too small for the data-cluster count")
        require(2 <= root_cluster < cluster_count + 2,
                "FAT32 root cluster is out of range")
        fat1_lba = start_lba + reserved
        fat2_lba = fat1_lba + fat_size
        first_fat_sector = read_sector(fat1_lba)
        second_fat_sector = read_sector(fat2_lba)
        require(first_fat_sector == second_fat_sector,
                "initialized FAT sectors differ")
        require(u32(first_fat_sector, 0) == 0x0FFFFFF8 and
                u32(first_fat_sector, 4) == 0x0FFFFFFF and
                u32(first_fat_sector, 8) == 0x0FFFFFFF,
                "FAT reserved entries or root allocation are invalid")

        fat_sector_cache: dict[int, bytes] = {}

        def fat_entry(cluster: int) -> int:
            require(2 <= cluster < cluster_count + 2,
                    f"cluster {cluster} is outside the data region")
            offset = cluster * 4
            sector_index, within = divmod(offset, sector_size)
            if sector_index not in fat_sector_cache:
                first = read_sector(fat1_lba + sector_index)
                second = read_sector(fat2_lba + sector_index)
                require(first == second,
                        f"FAT mirror mismatch in sector {sector_index}")
                fat_sector_cache[sector_index] = first
            return u32(fat_sector_cache[sector_index], within) & CLUSTER_MASK

        def cluster_data(cluster: int) -> bytes:
            first_lba = start_lba + first_data_offset + (cluster - 2) * spc
            require(first_lba + spc <= start_lba + total_sectors_fat,
                    f"cluster {cluster} escapes the FAT32 volume")
            return b"".join(read_sector(first_lba + offset) for offset in range(spc))

        def chain(first: int) -> list[int]:
            result: list[int] = []
            seen: set[int] = set()
            current = first
            while True:
                require(2 <= current < cluster_count + 2,
                        "cluster chain contains an out-of-range ID")
                require(current not in seen, "cluster chain contains a loop")
                seen.add(current)
                result.append(current)
                require(len(result) <= 65536,
                        "proof chain exceeds the production traversal bound")
                following = fat_entry(current)
                if following >= EOC:
                    return result
                require(2 <= following < cluster_count + 2,
                        "cluster chain points to a free or invalid entry")
                current = following

        def find_entry(first_cluster: int, name: bytes) -> bytes | None:
            for cluster in chain(first_cluster):
                data = cluster_data(cluster)
                for offset in range(0, len(data), 32):
                    entry = data[offset:offset + 32]
                    if entry[0] == 0:
                        return None
                    if entry[0] == 0xE5 or entry[11] == 0x0F:
                        continue
                    if entry[:11] == name:
                        return entry
            return None

        root_chain = chain(root_cluster)
        require(fat_entry(root_cluster) >= EOC,
                "fresh formatter did not allocate root cluster 2")
        directory = find_entry(root_cluster, DIRECTORY_NAME)
        require(directory is not None and directory[11] & 0x10,
                "DM9 proof directory is missing")
        directory_cluster = (u16(directory, 20) << 16) | u16(directory, 26)
        minimum_high_cluster = (120000 if sector_size == 512 else 65525)
        require(directory_cluster > minimum_high_cluster and
                fat_entry(directory_cluster) >= EOC,
                "directory allocation did not use a high FAT cluster")
        file_entry = find_entry(directory_cluster, FILE_NAME)
        require(file_entry is not None and not file_entry[11] & 0x10,
                "proof file is missing or is a directory")
        file_cluster = (u16(file_entry, 20) << 16) | u16(file_entry, 26)
        file_size = u32(file_entry, 28)
        require(file_size == PAYLOAD_BYTES and
                file_cluster > minimum_high_cluster,
                "large proof file size or starting cluster is invalid")
        file_chain = chain(file_cluster)
        cluster_bytes = spc * sector_size
        require(len(file_chain) == (file_size + cluster_bytes - 1) // cluster_bytes,
                "file chain length does not match its data size")
        contents = b"".join(cluster_data(cluster) for cluster in file_chain)[:file_size]
        require(contents == expected_payload(file_size),
                "large proof payload differs from the deterministic pattern")

    image_digest = hashlib.sha256()
    with image_path.open("rb") as hash_stream:
        for chunk in iter(lambda: hash_stream.read(1024 * 1024), b""):
            image_digest.update(chunk)
    print(f"image={image_path}")
    print(f"image_sha256={image_digest.hexdigest().upper()}")
    print(f"image_bytes={image_bytes}")
    print(f"logical_sector_size={sector_size}")
    print(f"partition=PASS number={partition_number} start_lba={start_lba} end_lba={end_lba} bytes={(end_lba - start_lba + 1) * sector_size}")
    print(f"fat32=PASS sectors={total_sectors_fat} bps={bps} spc={spc} cluster_bytes={spc * bps} clusters={cluster_count} fat_sectors={fat_size}")
    print(f"fsinfo=PASS next_free_hint={next_free} copies_agree=PASS backup_boot=PASS")
    print(f"fat_mirror=PASS sectors_checked={len(fat_sector_cache) + 1} root_cluster={root_cluster} directory_cluster={directory_cluster} file_cluster={file_cluster}")
    print(f"large_file=PASS payload_bytes={file_size} chain_clusters={len(file_chain)}")
    print("result=PASS read_only_on_demand_inspection=yes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, struct.error, UnicodeError, VerificationError) as error:
        print(f"result=FAIL detail={error}", file=sys.stderr)
        raise SystemExit(1)
